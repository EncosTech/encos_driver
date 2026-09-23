// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "ethernet_adapter.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "bus/bus.h"
#include "ethernet/protocol/gateway_payload.h"
#include "ethernet_slave.h"
#include "utils/thread_priority.h"

namespace encos {
EthernetAdapter::EthernetAdapter(const std::string& name, const std::string& logger, LogLevel level)
    : BaseAdapter(name, logger, level) {
    multi_ = name.find('@') == std::string::npos;
    ethernet::ConfigureHostInterface(name.substr(0, name.find('@')));
    Logger()->info("Ethernet host interface configured: 192.168.100.254/24");
    const auto endpoints = multi_ ? ethernet::DiscoverSlaves(name)
                                  : std::vector<ethernet::Endpoint>{ethernet::ParseEndpoint(name)};
    if (endpoints.empty())
        throw std::runtime_error("No Ethernet slaves discovered on " + name);
    for (const auto& endpoint : endpoints) {
        const unsigned slave = multi_ ? ethernet::SlaveId(endpoint.address) : 0;
        auto& peer = peers_[slave];
        peer.outgoing.reserve(ethernet::kMaxPayloadMessages);
        Logger()->info("Ethernet discovered slave={} interface={} address={}", slave,
                       endpoint.interface_name, endpoint.address);
        try {
            peer.management = std::make_unique<ethernet::ManagementClient>(
                endpoint, Logger(), [this, slave](unsigned channel) {
                    return !GetBus((static_cast<int>(slave) << 16) | static_cast<int>(channel))
                                ->GetMotors()
                                .empty();
                });
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
        for (int i = 0; i < 8; i++) {
            const int index = (static_cast<int>(peer.first) << 16) | i;
            buses[index] = GetBus(index);
        }
    return buses;
}
bool EthernetAdapter::Ok() {
    return running_.load();
}
void EthernetAdapter::Send(const MotorMessage& message) {
    SendSynchronized({message});
}
void EthernetAdapter::SendSynchronized(const MotorMessages& messages) {
    if (!Ok())
        throw std::runtime_error("Ethernet worker is not running");
    std::map<unsigned, MotorMessages> grouped;
    for (const auto& message : messages) {
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
    return new EthernetAdapter(name, logger, level);
}
}  // namespace encos
