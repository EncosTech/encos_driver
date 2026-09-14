#include "ethernet_management.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <stdexcept>
#include <thread>

#include "ethernet/protocol/gateway_wire.h"
#include "ethernet_slave.h"
#include "management_heartbeat.h"
#include "transport/datagram.h"

namespace encos::ethernet {
namespace {
using Clock = std::chrono::steady_clock;
uint32_t RequestId() {
    return static_cast<uint32_t>(Clock::now().time_since_epoch().count());
}

std::string CanErrorName(const CanError& e) {
    if (e.flags & (1U << 25))
        return "bus off";
    const auto lec = e.psr & 7U;
    const auto dlec = (e.psr >> 8) & 7U;
    if (lec == 1 || dlec == 1)
        return "stuff error";
    if (lec == 2 || dlec == 2)
        return "format error";
    if (lec == 3 || dlec == 3)
        return "ack error";
    if (lec == 4 || dlec == 4 || lec == 5 || dlec == 5)
        return "bit error";
    if (lec == 6 || dlec == 6)
        return "crc error";
    if (e.flags & (1U << 24))
        return "error passive";
    if (e.flags & (1U << 23))
        return "error warning";
    if (e.flags & (1U << 19))
        return "rx overflow";
    return "CAN error";
}
}  // namespace
std::array<uint8_t, 16> InitializeCanChannels(
    DatagramChannel& socket, const std::function<void(const CanError&)>& on_event) {
    constexpr uint32_t kNominalBps = 1000000, kDataBps = 5000000;
    constexpr uint32_t kNominalSamplePoint = 750, kDataSamplePoint = 875;
    std::array<uint8_t, 16> identity{};
    try {
        for (unsigned step = 0; step <= 8; ++step) {
            const bool subscribe = step == 0;
            const unsigned channel = subscribe ? 0 : step - 1;
            const uint32_t request = RequestId();
            std::array<uint8_t, 64> out{};
            gw_packet(out.data(), subscribe ? GW_SUBSCRIBE : GW_CAN_SET, request,
                      subscribe ? nullptr : identity.data());
            if (!subscribe) {
                gw_put32(out.data() + 28, channel);
                gw_put32(out.data() + 32, kNominalBps);
                gw_put32(out.data() + 36, kDataBps);
                gw_put32(out.data() + 40, kNominalSamplePoint);
                gw_put32(out.data() + 44, kDataSamplePoint);
            }
            const auto deadline = Clock::now() + std::chrono::seconds(2);
            auto next = Clock::now();
            bool acknowledged = false;
            while (Clock::now() < deadline) {
                if (Clock::now() >= next) {
                    (void) socket.Send(out.data(), out.size());
                    next = Clock::now() + std::chrono::milliseconds(200);
                }
                if (!socket.Wait(50))
                    continue;
                std::array<uint8_t, 65> reply{};
                const auto size = socket.Receive(reply.data(), reply.size());
                if (size != 64 || !gw_packet_valid(reply.data(), 64))
                    continue;
                if (!subscribe && reply[5] == GW_CAN_EVENT) {
                    const auto event = DecodeCanError(reply.data(), 64, identity);
                    if (event && on_event)
                        on_event(*event);
                    continue;
                }
                if (reply[5] != (subscribe ? GW_SUBSCRIBED : GW_CAN_STATE) ||
                    gw_get32(reply.data() + 8) != request)
                    continue;
                if (subscribe) {
                    if (gw_get32(reply.data() + 52) != 0 || !(reply[46] & 4) || reply[47] != 8)
                        throw std::runtime_error(
                            "gateway does not support eight-channel CAN initialization or "
                            "subscription rejected");
                    std::copy_n(reply.data() + 12, 16, identity.begin());
                    if (std::all_of(identity.begin(), identity.end(), [](uint8_t value) {
                            return value == 0;
                        }))
                        throw std::runtime_error("gateway returned an empty identity");
                } else {
                    if (std::memcmp(reply.data() + 12, identity.data(), 16) != 0 ||
                        gw_get32(reply.data() + 28) != channel)
                        continue;
                    const auto status = gw_get32(reply.data() + 48);
                    if (status != 0 || gw_get32(reply.data() + 52) != 1 ||
                        std::memcmp(reply.data() + 32, out.data() + 32, 16) != 0)
                        throw std::runtime_error(
                            "CAN" + std::to_string(channel) +
                            " initialization rejected or timing mismatch, status=" +
                            std::to_string(status));
                }
                acknowledged = true;
                break;
            }
            if (!acknowledged)
                throw std::runtime_error(
                    subscribe ? "gateway CAN initialization handshake timed out"
                              : "CAN" + std::to_string(channel) + " initialization timed out");
        }
    } catch (...) {
        std::array<uint8_t, 64> unsubscribe{};
        gw_packet(unsubscribe.data(), GW_UNSUBSCRIBE, RequestId(), identity.data());
        try {
            (void) socket.Send(unsubscribe.data(), unsubscribe.size());
        } catch (...) {}
        throw;
    }
    return identity;
}
std::vector<Endpoint> DiscoverSlaves(const std::string& interface_name) {
    auto channel = OpenDiscovery(interface_name);
    return DiscoverSlaves(*channel, interface_name);
}
std::vector<Endpoint> DiscoverSlaves(DiscoveryChannel& channel, const std::string& interface_name) {
    const auto request = RequestId();
    std::array<uint8_t, 64> query{};
    gw_packet(query.data(), GW_DISCOVER, request, nullptr);
    channel.Broadcast(query.data(), query.size());
    const auto begin = Clock::now();
    bool retried = false;
    std::map<std::string, std::array<uint8_t, 16>> identities;
    std::map<std::string, Endpoint> found;
    while (Clock::now() - begin < std::chrono::milliseconds(800)) {
        if (!retried && Clock::now() - begin > std::chrono::milliseconds(300)) {
            channel.Broadcast(query.data(), query.size());
            retried = true;
        }
        const auto datagram = channel.Receive(50);
        const auto& packet = datagram.payload;
        if (packet.size() != 64 || !gw_packet_valid(packet.data(), packet.size()) ||
            packet[5] != GW_INFO || gw_get32(packet.data() + 8) != request ||
            datagram.port != GW_MGMT_PORT)
            continue;
        const auto ip = FormatIPv4(packet.data() + 28);
        if (ip != datagram.address)
            continue;
        std::array<uint8_t, 16> id{};
        std::copy_n(packet.data() + 12, 16, id.begin());
        if (std::all_of(id.begin(), id.end(), [](uint8_t value) {
                return value == 0;
            }))
            continue;
        if (!interface_name.empty() && interface_name != datagram.interface_name)
            continue;
        if (packet[32] != 255 || packet[33] != 255 || packet[34] != 255 || packet[35] != 0 ||
            packet[47] != 8)
            continue;
        try {
            (void) SlaveId(ip);
        } catch (const std::invalid_argument&) {
            continue;
        }
        const auto key = datagram.interface_name + "@" + ip;
        const auto old = identities.find(key);
        if (old != identities.end() && old->second != id)
            throw std::runtime_error("Duplicate Ethernet slave ID on " + key);
        identities[key] = id;
        found.emplace(key, Endpoint{datagram.interface_name, ip, 5000});
    }
    std::vector<Endpoint> result;
    for (const auto& entry : found)
        result.push_back(entry.second);
    return result;
}
struct ManagementClient::Impl {
    std::unique_ptr<DatagramChannel> sock;
    LoggerPtr logger;
    std::string peer_name;
    std::function<bool(unsigned)> has_devices;
    std::atomic<bool> running{true};
    std::thread worker;
    std::array<uint8_t, 16> identity{};
    explicit Impl(const Endpoint& endpoint, LoggerPtr log, std::function<bool(unsigned)> devices)
        : logger(std::move(log)), peer_name(endpoint.address), has_devices(std::move(devices)) {
        auto management = endpoint;
        management.port = GW_MGMT_PORT;
        sock = ConnectDatagram(management);
        identity = InitializeCanChannels(*sock);
        logger->info(
            "Ethernet slave={} CAN0-7 initialized: nominal=1000000 sample=75%, data=5000000 "
            "sample=87.5%",
            peer_name);
        worker = std::thread([this] {
            try {
                Loop();
            } catch (const std::exception& e) {
                try {
                    logger->error(std::string("Ethernet management stopped: ") + e.what());
                } catch (...) {}
            } catch (...) {
                // A management failure must not terminate the CAN transport process.
            }
        });
    }
    ~Impl() {
        running = false;
        if (worker.joinable())
            worker.join();
    }
    void Loop() {
        if (!SetManagementThreadScheduling()) {
            logger->warn("Ethernet management could not select normal scheduling");
            return;
        }
        auto next = Clock::now();
        ManagementHeartbeat heartbeat(next);
        bool warned = false;
        uint32_t request = RequestId();
        std::array<uint32_t, 8> recoveries{};
        std::array<Clock::time_point, 8> last_error_logs{};
        std::array<std::string, 8> last_error_names{};
        while (running) {
            const auto now = Clock::now();
            if (heartbeat.CheckTimeout(now)) {
                logger->error("Ethernet slave={} disconnected: heartbeat timeout (500 ms)",
                              SlaveId(peer_name));
            }
            if (now >= next) {
                std::array<uint8_t, 64> out{};
                ++request;
                gw_packet(out.data(), GW_SUBSCRIBE, request, identity.data());
                (void) sock->Send(out.data(), out.size());
                next = now + std::chrono::seconds(3);
            }
            if (!sock->Wait(10))
                continue;
            std::array<uint8_t, 65> packet{};
            const auto n = sock->Receive(packet.data(), packet.size());
            if (n != 64 || !gw_packet_valid(packet.data(), 64))
                continue;
            if (packet[5] == GW_SUBSCRIBED && gw_get32(packet.data() + 8) == request &&
                std::memcmp(packet.data() + 12, identity.data(), identity.size()) == 0) {
                if (gw_get32(packet.data() + 52) != 0) {
                    if (!warned)
                        logger->warn("Ethernet CAN error subscription rejected: subscriber limit");
                    warned = true;
                    continue;
                }
                warned = false;
                continue;
            }
            const auto event = DecodeCanError(packet.data(), 64, identity);
            if (!event)
                continue;
            const bool was_online = heartbeat.Online();
            if (!heartbeat.Report(event->sequence, event->uptime, Clock::now()))
                continue;
            if (!was_online)
                logger->warn("Ethernet slave={} connection restored", SlaveId(peer_name));
            const auto& e = *event;
            if (!has_devices || !has_devices(e.channel)) {
                recoveries[e.channel] = e.recovered;
                continue;
            }
            if (e.flags != 0) {
                const auto name = CanErrorName(e);
                const auto now_error = Clock::now();
                if (name != last_error_names[e.channel] ||
                    now_error - last_error_logs[e.channel] >= std::chrono::seconds(1)) {
                    logger->error("slave={} CAN{}: {}", SlaveId(peer_name), e.channel, name);
                    last_error_names[e.channel] = name;
                    last_error_logs[e.channel] = now_error;
                }
            }
            if (e.recovered != recoveries[e.channel]) {
                recoveries[e.channel] = e.recovered;
                if (e.flags == 0 && !last_error_names[e.channel].empty()) {
                    logger->warn("slave={} CAN{}: controller reinitialized", SlaveId(peer_name),
                                 e.channel);
                    last_error_names[e.channel].clear();
                }
            }
        }
        std::array<uint8_t, 64> out{};
        gw_packet(out.data(), GW_UNSUBSCRIBE, RequestId(), identity.data());
        (void) sock->Send(out.data(), out.size());
    }
};
ManagementClient::ManagementClient(const Endpoint& endpoint, LoggerPtr logger,
                                   std::function<bool(unsigned)> has_devices)
    : impl_(std::make_unique<Impl>(endpoint, std::move(logger), std::move(has_devices))) {}
ManagementClient::~ManagementClient() = default;
}  // namespace encos::ethernet
