// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "ethernet_adapter.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <stdexcept>

#include "adapter/adapter_extend.h"
#include "bus/bus.h"
#include "can_statistics.h"
#include "encos/driver_manager.h"
#include "ethernet/protocol/gateway_payload.h"
#include "ethernet_slave.h"
#include "platform/os.h"
#include "utils/adapter_extend_update.h"
#include "utils/thread_priority.h"
#include "utils/tracy.h"

namespace encos {
EthernetAdapter::EthernetAdapter(const std::string& name, const std::string& logger, LogLevel level,
                                 const AdapterExtend& extend)
    : BaseAdapter(name, logger, level) {
    multi_ = name.find('@') == std::string::npos;
    const auto explicit_endpoint =
        multi_ ? std::optional<ethernet::Endpoint>{} : ethernet::ParseEndpoint(name);
    const auto exclusive = extend.Common().value("host_network_exclusive", false);
    host_interface_ = ethernet::ConfigureHostInterface(name.substr(0, name.find('@')), exclusive);
    if (host_interface_)
        host_interface_->ValidateRoute(explicit_endpoint ? explicit_endpoint->address
                                                         : "255.255.255.255");
    Logger()->info("Ethernet host interface configured: 192.168.100.254/24");
    const auto endpoints = explicit_endpoint ? std::vector<ethernet::Endpoint>{*explicit_endpoint}
                                             : ethernet::DiscoverSlaves(name);
    if (endpoints.empty())
        throw std::runtime_error("No Ethernet slaves discovered on " + name);
    if (host_interface_ && multi_) {
        for (const auto& endpoint : endpoints)
            host_interface_->ValidateRoute(endpoint.address);
    }

    // 总线标识为 (slave, channel)：先校验信封没有指向未发现从站或超范围的通道
    std::set<std::pair<int, int>> available_buses;
    for (const auto& endpoint : endpoints) {
        const int slave = static_cast<int>(multi_ ? ethernet::SlaveId(endpoint.address) : 0U);
        for (unsigned channel = 0; channel < ethernet::kGatewayChannelCount; ++channel) {
            available_buses.emplace(slave, static_cast<int>(channel));
        }
    }
    for (const auto& bus : extend.DeclaredBuses()) {
        if (available_buses.count(bus) == 0) {
            throw std::runtime_error("Ethernet adapter has no bus '" + std::to_string(bus.first) +
                                     ":" + std::to_string(bus.second) + "'");
        }
    }

    AdapterStatistics statistics;
    for (const auto& endpoint : endpoints) {
        const unsigned slave = multi_ ? ethernet::SlaveId(endpoint.address) : 0;
        auto& peer = peers_[slave];
        Logger()->info("Ethernet discovered slave={} interface={} address={}", slave,
                       endpoint.interface_name, endpoint.address);
        try {
            peer.management = std::make_unique<ethernet::ManagementClient>(
                endpoint, Logger(),
                [&extend, slave, &peer](unsigned count) {
                    const auto first = extend.Scope(static_cast<int>(slave), 0);
                    const auto mode = first.value("protocol", std::string("auto"));
                    const auto budget = first.value("max_payload", int64_t{1448});
                    if (!first.value("max_payload", nlohmann::json(1448)).is_number_integer() ||
                        budget < 128 || budget > 1472)
                        throw std::invalid_argument(
                            "Ethernet max_payload must be an integer in 128..1472");
                    peer.protocol_mode = ParseRelayProtocolMode(mode);
                    peer.max_payload = static_cast<std::size_t>(budget);
                    peer.wire.session_id = static_cast<uint32_t>(
                        std::chrono::steady_clock::now().time_since_epoch().count());
                    for (unsigned channel = 1; channel < count; ++channel) {
                        const auto scope =
                            extend.Scope(static_cast<int>(slave), static_cast<int>(channel));
                        if (!scope.value("max_payload", nlohmann::json(1448)).is_number_integer() ||
                            scope.value("protocol", std::string("auto")) != mode ||
                            scope.value("max_payload", int64_t{1448}) != budget)
                            throw std::invalid_argument(
                                "Ethernet protocol/max_payload must agree across peer channels");
                    }
                    return ethernet::BuildGatewayChannelTimings(extend, static_cast<int>(slave),
                                                                count);
                },
                [this, slave](unsigned channel) {
                    return HasKnownMotors(slave, channel);
                },
                [this, slave, cached = std::array<BusStatistics, 8>{},
                 ordering = std::array<ethernet::CanStatisticsOrder, 8>{}](
                    const ethernet::CanError& event) mutable {
                    const auto result = ordering[event.channel].Observe(
                        event, ethernet::CanStatisticsOrder::Clock::now());
                    if (result == ethernet::CanStatisticsOrder::Result::Rejected)
                        return;

                    auto& bus = cached[event.channel];
                    if (result == ethernet::CanStatisticsOrder::Result::Restarted)
                        bus = BusStatistics{};

                    bus.slave_id = static_cast<int>(slave);
                    bus.bus_id = static_cast<int>(event.channel);

                    ethernet::ApplyCanStatistics(bus, event);
                    UpdateStatistics(bus);
                },
                false,
                [&peer] {
                    return peer.protocol_mode == RelayProtocolMode::Auto;
                });

            const auto& negotiated = peer.management->Capabilities();
            const auto selected = SelectRelayProtocol(peer.protocol_mode, negotiated);
            const auto budget =
                std::min(peer.max_payload, selected == RelayProtocol::Emr1
                                               ? std::size_t(1448)
                                               : (peer.protocol_mode == RelayProtocolMode::Auto
                                                      ? std::size_t(negotiated.max_payload)
                                                      : peer.max_payload));
            Logger()->info(
                "Ethernet slave={} ip={} EMR negotiation: requested={} selected={} "
                "max_payload={} emr2_capability={} advertised_max_payload={} source={}",
                slave, endpoint.address,
                peer.protocol_mode == RelayProtocolMode::Auto
                    ? "auto"
                    : (peer.protocol_mode == RelayProtocolMode::Emr1 ? "EMR1" : "EMR2"),
                selected == RelayProtocol::Emr1 ? "EMR1" : "EMR2", budget,
                negotiated.emr2 ? "confirmed" : "unconfirmed",
                negotiated.emr2 ? std::to_string(negotiated.max_payload) : "unknown",
                peer.protocol_mode != RelayProtocolMode::Auto
                    ? "explicit configuration"
                    : (negotiated.emr2 ? "capability reply" : "legacy fallback"));

            const auto& initialized = peer.management->Initialization();
            peer.channel_count = initialized.channel_count;

            for (unsigned channel = 0; channel < peer.channel_count; ++channel) {
                BusStatistics bus;
                bus.slave_id = static_cast<int>(slave);
                bus.bus_id = static_cast<int>(channel);
                statistics.buses.push_back(bus);
            }

            timings_[slave] = initialized.timings;
        } catch (const std::exception& error) {
            Logger()->error("Slave {} CAN initialization failed: {}", slave, error.what());
            throw;
        }
    }
    InitializeStatistics(statistics, 500);

    for (auto& peer : peers_)
        peer.second.management->Start();

    transport_ = ethernet::CreateTransport(endpoints, multi_);
    running_ = true;
    worker_ = std::thread(&EthernetAdapter::Worker, this);
}

EthernetAdapter::~EthernetAdapter() {
    running_ = false;
    if (transport_)
        transport_->Notify();
    if (worker_.joinable())
        worker_.join();
}

std::unordered_map<int, Bus*> EthernetAdapter::GetBuses() {
    std::unordered_map<int, Bus*> buses;
    for (const auto& peer : peers_)
        for (unsigned i = 0; i < peer.second.channel_count; ++i) {
            const int index = (static_cast<int>(peer.first) << 16) | i;
            buses[index] = GetBus(index);
        }
    return buses;
}

bool EthernetAdapter::IsValidBus(int index) const {
    if (index < 0)
        return false;
    const auto slave = multi_ ? static_cast<unsigned>(index) >> 16 : 0U;
    const auto channel =
        multi_ ? static_cast<unsigned>(index) & 0xffffU : static_cast<unsigned>(index);
    const auto peer = peers_.find(slave);
    return peer != peers_.end() && channel < peer->second.channel_count;
}

bool EthernetAdapter::HasKnownMotors(unsigned slave, unsigned channel) {
    const auto buses = GetKnownBusesSnapshot();
    const auto bus = buses.find((static_cast<int>(slave) << 16) | static_cast<int>(channel));
    return bus != buses.end() && !EncosDriverManager::Instance().GetMotors(bus->second).empty();
}

bool EthernetAdapter::Ok() {
    return running_.load();
}

bool EthernetAdapter::UpdateBusExtend(const std::string& extend_json) {
    const auto extend = AdapterExtend::Parse(extend_json);
    adapter_update::WarnIgnoredCommonFields(extend, Logger());
    for (const auto& bus : extend.DeclaredBuses()) {
        const auto* changes = extend.BusOverride(bus.first, bus.second);
        if (changes && (changes->contains("protocol") || changes->contains("max_payload")))
            throw std::invalid_argument("Ethernet wire settings require adapter recreation");
        if (!IsValidBus((bus.first << 16) | bus.second)) {
            throw std::runtime_error("Ethernet adapter has no bus '" + std::to_string(bus.first) +
                                     ":" + std::to_string(bus.second) + "'");
        }
    }
    for (const auto& bus : extend.DeclaredBuses()) {
        const auto* override_json = extend.BusOverride(bus.first, bus.second);
        if (override_json == nullptr) {
            continue;
        }
        if (bus.first < 0 || bus.second < 0 ||
            static_cast<unsigned>(bus.second) >= ethernet::kGatewayChannelCount) {
            throw std::runtime_error("Ethernet adapter has no bus '" + std::to_string(bus.first) +
                                     ":" + std::to_string(bus.second) + "'");
        }
        const auto peer = peers_.find(static_cast<unsigned>(bus.first));
        if (peer == peers_.end() || peer->second.management == nullptr) {
            throw std::runtime_error("Ethernet adapter has no bus '" + std::to_string(bus.first) +
                                     ":" + std::to_string(bus.second) + "'");
        }
        if (override_json->contains("protocol") || override_json->contains("max_payload"))
            throw std::invalid_argument("Ethernet wire settings require adapter recreation");
        const unsigned channel = static_cast<unsigned>(bus.second);
        ethernet::GatewayCanTiming timing;
        {
            std::lock_guard<std::mutex> lock(timings_mutex_);
            const auto cached = timings_.find(peer->first);
            if (cached == timings_.end()) {
                throw std::runtime_error("Ethernet adapter has no live timing for slave " +
                                         std::to_string(peer->first));
            }
            timing = cached->second[channel];
        }
        ethernet::ApplyGatewayCanTiming(*override_json, &timing);
        peer->second.management->ReconfigureChannel(channel, timing);
        Logger()->info("Ethernet slave={} CAN{} timing updated: {}/{} bps, {}/{} per-mille",
                       peer->first, channel, timing.nominal_bps, timing.data_bps,
                       ethernet::ToPerMille(timing.nominal_sample_point),
                       ethernet::ToPerMille(timing.data_sample_point));
        {
            std::lock_guard<std::mutex> lock(timings_mutex_);
            timings_[peer->first][channel] = timing;
        }
    }
    return true;
}

void EthernetAdapter::Send(const MotorMessage& message) {
    SendSynchronized({message});
}

void EthernetAdapter::SendSynchronized(const MotorMessages& messages) {
    ENCOS_TRACY_ZONE("Ethernet::QueueBatch");
    if (!Ok())
        throw std::runtime_error("Ethernet worker is not running");

    std::map<unsigned, MotorMessages> grouped;
    for (const auto& message : messages) {
        if (!IsValidBus(message.bus_idx)) {
            throw std::invalid_argument("Ethernet adapter has no bus " +
                                        std::to_string(message.bus_idx));
        }
        const unsigned slave = multi_ ? ethernet::BusSlave(message.bus_idx) : 0;
        if (peers_.find(slave) == peers_.end())
            throw std::invalid_argument("Ethernet slave was not discovered: " +
                                        std::to_string(slave));
        auto local = message;
        if (multi_)
            local.bus_idx &= 0xffff;
        grouped[slave].push_back(local);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& group : grouped) {
            auto& peer = peers_.at(group.first);
            auto options = peer.wire;
            const auto caps =
                peer.management ? peer.management->Capabilities() : RelayCapabilities{};
            options.protocol = SelectRelayProtocol(peer.protocol_mode, caps);
            options.max_payload =
                std::min(peer.max_payload, options.protocol == RelayProtocol::Emr1
                                               ? std::size_t(1448)
                                               : (peer.protocol_mode == RelayProtocolMode::Auto
                                                      ? std::size_t(caps.max_payload)
                                                      : peer.max_payload));
            (void) EncodeRelayPackets(RelayFrameType::RelayToHelper, group.second, options);
            if (group.second.size() > peer.pending.Available())
                throw std::runtime_error("Ethernet transmit queue full");
        }
        for (auto& group : grouped)
            if (!peers_.at(group.first).pending.Push(std::move(group.second)))
                throw std::logic_error("Ethernet transmit queue invariant violated");
    }
    if (!messages.empty())
        transport_->Notify();
}

void EthernetAdapter::Worker() {
    try {
        if (!utils::SetCurrentThreadPriority(50)) {
            Logger()->warn(
                "Ethernet worker priority setup failed; check platform thread-priority "
                "permissions");
        } else {
            Logger()->info("Ethernet worker priority configured");
        }
        while (running_) {
            ENCOS_TRACY_ZONE("Ethernet::WorkerCycle");
            ENCOS_TRACY_FRAME("Ethernet");

            bool sent = false;
            for (auto& entry : peers_) {
                auto& peer = entry.second;
                if (peer.protocol_mode == RelayProtocolMode::Auto && peer.management &&
                    !peer.management->CapabilitiesReady())
                    continue;
                const auto caps =
                    peer.management ? peer.management->Capabilities() : RelayCapabilities{};
                const auto protocol = SelectRelayProtocol(peer.protocol_mode, caps);
                if (!peer.packets.empty() && peer.wire.protocol != protocol)
                    throw std::runtime_error(
                        "Ethernet capabilities expired with queued data; rebuild adapter");
                if (peer.packets.empty()) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto batch = peer.pending.Pop();
                    if (!batch.empty()) {
                        peer.wire.protocol = protocol;
                        peer.wire.max_payload = std::min(
                            peer.max_payload, protocol == RelayProtocol::Emr1
                                                  ? std::size_t(1448)
                                                  : (peer.protocol_mode == RelayProtocolMode::Auto
                                                         ? std::size_t(caps.max_payload)
                                                         : peer.max_payload));
                        auto packets =
                            EncodeRelayPackets(RelayFrameType::RelayToHelper, batch, peer.wire);
                        ++peer.wire.batch_id;
                        for (auto& packet : packets)
                            peer.packets.push_back(std::move(packet));
                    }
                }
                if (!peer.packets.empty() &&
                    transport_->SendPayload(entry.first, peer.packets.front())) {
                    peer.packets.pop_front();
                    sent = true;
                }
                const auto now = std::chrono::steady_clock::now();
                if (now >= peer.heartbeat && peer.packets.empty()) {
                    std::vector<uint8_t> heartbeat;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        auto options = peer.wire;
                        options.protocol = protocol;
                        heartbeat = EncodeRelayPackets(RelayFrameType::RelayToHelper,
                                                       MotorMessages{}, options)
                                        .front();
                        peer.wire.sequence = options.sequence;
                    }
                    if (transport_->SendPayload(entry.first, heartbeat))
                        peer.heartbeat = now + std::chrono::seconds(1);
                }
            }
            auto received = transport_->Receive();
            received.erase(std::remove_if(received.begin(), received.end(),
                                          [this](const auto& message) {
                                              return !IsValidBus(message.bus_idx);
                                          }),
                           received.end());
            if (!received.empty()) {
                ENCOS_TRACY_ZONE("Ethernet::MessageCallback");
                OnMessage(received);
            }

            if (!sent && received.empty())
                transport_->Wait(1);
        }
    } catch (const std::exception& error) {
        Logger()->error("Ethernet worker stopped: {}", error.what());
        running_ = false;
    }
}

BaseAdapter* CreateEthernetAdapterStatic(const std::string& name, const std::string& logger,
                                         LogLevel level) {
    // 委托给 extend 入口，使默认时序只在 GatewayCanTiming 中声明一次
    return CreateEthernetAdapterStaticWithExtend(name, logger, level, "");
}

BaseAdapter* CreateEthernetAdapterStaticWithExtend(const std::string& name,
                                                   const std::string& logger, LogLevel level,
                                                   const std::string& extend_json) {
    return new EthernetAdapter(name, logger, level, AdapterExtend::Parse(extend_json));
}

void EnumerateEthernetInterfaces(AdapterInterfaceEmit emit, void* context) {
    if (emit == nullptr) {
        return;
    }
    for (const auto& endpoint : platform::GetWiredInterfaceNames()) {
        emit(endpoint.c_str(), context);
    }
}
}  // namespace encos
