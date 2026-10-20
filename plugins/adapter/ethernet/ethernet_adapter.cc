// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "ethernet_adapter.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <stdexcept>

#include "adapter/adapter_extend.h"
#include "bus/bus.h"
#include "ethernet/protocol/gateway_payload.h"
#include "ethernet_slave.h"
#include "platform/os.h"
#include "utils/adapter_extend_update.h"
#include "utils/thread_priority.h"

namespace encos {
EthernetAdapter::EthernetAdapter(const std::string& name, const std::string& logger, LogLevel level,
                                 const AdapterExtend& extend)
    : BaseAdapter(name, logger, level) {
    multi_ = name.find('@') == std::string::npos;
    ethernet::ConfigureHostInterface(name.substr(0, name.find('@')));
    Logger()->info("Ethernet host interface configured: 192.168.100.254/24");
    const auto endpoints = multi_ ? ethernet::DiscoverSlaves(name)
                                  : std::vector<ethernet::Endpoint>{ethernet::ParseEndpoint(name)};
    if (endpoints.empty())
        throw std::runtime_error("No Ethernet slaves discovered on " + name);

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

    for (const auto& endpoint : endpoints) {
        const unsigned slave = multi_ ? ethernet::SlaveId(endpoint.address) : 0;
        auto& peer = peers_[slave];
        peer.outgoing.reserve(ethernet::kMaxPayloadMessages);
        Logger()->info("Ethernet discovered slave={} interface={} address={}", slave,
                       endpoint.interface_name, endpoint.address);
        try {
            peer.management = std::make_unique<ethernet::ManagementClient>(
                endpoint, Logger(),
                [&extend, slave](unsigned count) {
                    return ethernet::BuildGatewayChannelTimings(extend, static_cast<int>(slave),
                                                                count);
                },
                [this, slave](unsigned channel) {
                    return !GetBus((static_cast<int>(slave) << 16) | static_cast<int>(channel))
                                ->GetMotors()
                                .empty();
                });
            const auto& initialized = peer.management->Initialization();
            peer.channel_count = initialized.channel_count;
            timings_[slave] = initialized.timings;
        } catch (const std::exception& error) {
            Logger()->error("Slave {} CAN initialization failed: {}", slave, error.what());
            throw;
        }
    }
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
bool EthernetAdapter::Ok() {
    return running_.load();
}

bool EthernetAdapter::UpdateBusExtend(const std::string& extend_json) {
    const auto extend = AdapterExtend::Parse(extend_json);
    adapter_update::WarnIgnoredCommonFields(extend, Logger());
    for (const auto& bus : extend.DeclaredBuses()) {
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
    std::map<unsigned, std::vector<MotorMessages>> batches;
    for (const auto& group : grouped)
        for (std::size_t offset = 0; offset < group.second.size();
             offset += ethernet::kMaxPayloadMessages) {
            const auto count =
                std::min(ethernet::kMaxPayloadMessages, group.second.size() - offset);
            MotorMessages batch(group.second.begin() + offset,
                                group.second.begin() + offset + count);
            (void) ethernet::EncodePayload(batch);
            batches[group.first].push_back(std::move(batch));
        }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& group : grouped)
            if (group.second.size() > peers_.at(group.first).pending.Available())
                throw std::runtime_error("Ethernet transmit queue full");
        for (auto& group : batches)
            for (auto& batch : group.second)
                if (!peers_.at(group.first).pending.Push(std::move(batch)))
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
            bool sent = false;
            for (auto& entry : peers_) {
                auto& peer = entry.second;
                if (peer.outgoing.empty()) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    peer.outgoing = peer.pending.Pop();
                }
                if (!peer.outgoing.empty() && transport_->Send(entry.first, peer.outgoing)) {
                    peer.outgoing.clear();
                    sent = true;
                }
                const auto now = std::chrono::steady_clock::now();
                if (now >= peer.heartbeat && transport_->Send(entry.first, {}))
                    peer.heartbeat = now + std::chrono::seconds(1);
            }
            auto received = transport_->Receive();
            received.erase(std::remove_if(received.begin(), received.end(),
                                          [this](const auto& message) {
                                              return !IsValidBus(message.bus_idx);
                                          }),
                           received.end());
            if (!received.empty())
                OnMessage(received);
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
