// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "ethernet_management.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

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
namespace {
/**
 * @brief 在已订阅的管理通道上按通道下发 GW_CAN_SET 并校验网关回读
 * @throws std::runtime_error 被网关拒绝或超时
 *
 * 与初始化走同一套重发节奏（200 ms 重发 / 2 s 截止）与逐字节回读校验，
 * 因此在线重配和启动配置的判定标准完全一致。
 */
void ConfigureCanChannel(DatagramChannel& socket, unsigned channel, const GatewayCanTiming& timing,
                         const std::array<uint8_t, 16>& identity,
                         const std::function<void(const CanError&)>& on_event) {
    const uint32_t request = RequestId();
    std::array<uint8_t, 64> out{};
    gw_packet(out.data(), GW_CAN_SET, request, identity.data());
    gw_put32(out.data() + 28, channel);
    gw_put32(out.data() + 32, timing.nominal_bps);
    gw_put32(out.data() + 36, timing.data_bps);
    gw_put32(out.data() + 40, ToPerMille(timing.nominal_sample_point));
    gw_put32(out.data() + 44, ToPerMille(timing.data_sample_point));

    const auto deadline = Clock::now() + std::chrono::seconds(2);
    auto next = Clock::now();
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
        if (reply[5] == GW_CAN_EVENT) {
            const auto event = DecodeCanError(reply.data(), 64, identity);
            if (event && on_event)
                on_event(*event);
            continue;
        }
        if (reply[5] != GW_CAN_STATE || gw_get32(reply.data() + 8) != request)
            continue;
        if (std::memcmp(reply.data() + 12, identity.data(), 16) != 0 ||
            gw_get32(reply.data() + 28) != channel)
            continue;
        const auto status = gw_get32(reply.data() + 48);
        if (status != 0 || gw_get32(reply.data() + 52) != 1 ||
            std::memcmp(reply.data() + 32, out.data() + 32, 16) != 0) {
            throw std::runtime_error(
                "CAN" + std::to_string(channel) +
                " initialization rejected or timing mismatch, status=" + std::to_string(status));
        }
        return;
    }
    throw std::runtime_error("CAN" + std::to_string(channel) + " initialization timed out");
}
}  // namespace

GatewayInitialization InitializeCanChannels(DatagramChannel& socket,
                                            const TimingFactory& make_timings,
                                            const std::function<void(const CanError&)>& on_event) {
    GatewayInitialization result;
    auto& identity = result.identity;
    try {
        // 先订阅：后续每条 GW_CAN_SET 都必须带上订阅应答里的身份。
        {
            const uint32_t request = RequestId();
            std::array<uint8_t, 64> out{};
            gw_packet(out.data(), GW_SUBSCRIBE, request, nullptr);
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
                if (reply[5] != GW_SUBSCRIBED || gw_get32(reply.data() + 8) != request)
                    continue;
                if (gw_get32(reply.data() + 52) != 0 || !(reply[46] & 4) || reply[47] == 0 ||
                    reply[47] > kGatewayChannelCount)
                    throw std::runtime_error(
                        "gateway reports unsupported CAN capabilities or "
                        "subscription rejected");
                std::copy_n(reply.data() + 12, 16, identity.begin());
                if (std::all_of(identity.begin(), identity.end(), [](uint8_t value) {
                        return value == 0;
                    }))
                    throw std::runtime_error("gateway returned an empty identity");
                result.channel_count = reply[47];
                acknowledged = true;
                break;
            }
            if (!acknowledged)
                throw std::runtime_error("gateway CAN initialization handshake timed out");
        }

        result.timings = make_timings(result.channel_count);
        for (unsigned channel = 0; channel < result.channel_count; ++channel) {
            ConfigureCanChannel(socket, channel, result.timings[channel], identity, on_event);
        }
    } catch (...) {
        std::array<uint8_t, 64> unsubscribe{};
        gw_packet(unsubscribe.data(), GW_UNSUBSCRIBE, RequestId(), identity.data());
        try {
            (void) socket.Send(unsubscribe.data(), unsubscribe.size());
        } catch (...) {}
        throw;
    }
    return result;
}
GatewayInitialization InitializeCanChannels(DatagramChannel& socket,
                                            const GatewayChannelTimings& timings,
                                            const std::function<void(const CanError&)>& on_event) {
    return InitializeCanChannels(
        socket,
        [&](unsigned) {
            return timings;
        },
        on_event);
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
            (packet[47] == 0 || packet[47] > kGatewayChannelCount))
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
    GatewayInitialization initialization;
    std::array<uint8_t, 16> identity{};
    // 重配请求槽：调用方入队，Loop() 在管理线程上执行——同一个 UDP 套接字只能有一个
    // 读者，两个线程各自 Wait/Receive 会互相吞掉应答。
    std::mutex request_mutex;
    struct ReconfigureRequest {
        unsigned channel = 0;
        GatewayCanTiming timing;
        std::promise<void> completion;
    };
    std::deque<ReconfigureRequest> pending_requests;

    void FailPendingRequests() {
        std::lock_guard<std::mutex> lock(request_mutex);
        running = false;
        for (auto& request : pending_requests) {
            request.completion.set_exception(
                std::make_exception_ptr(std::runtime_error("Ethernet management is stopping")));
        }
        pending_requests.clear();
    }
    explicit Impl(const Endpoint& endpoint, LoggerPtr log, const TimingFactory& make_timings,
                  std::function<bool(unsigned)> devices)
        : logger(std::move(log)), peer_name(endpoint.address), has_devices(std::move(devices)) {
        auto management = endpoint;
        management.port = GW_MGMT_PORT;
        sock = ConnectDatagram(management);
        initialization = InitializeCanChannels(*sock, make_timings);
        identity = initialization.identity;
        for (unsigned channel = 0; channel < initialization.channel_count; ++channel) {
            const auto& timing = initialization.timings[channel];
            logger->info(
                "Ethernet slave={} CAN{} initialized: nominal={} sample={}/1000, data={} "
                "sample={}/1000",
                peer_name, channel, timing.nominal_bps, ToPerMille(timing.nominal_sample_point),
                timing.data_bps, ToPerMille(timing.data_sample_point));
        }
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
            FailPendingRequests();
        });
    }
    ~Impl() {
        FailPendingRequests();
        if (worker.joinable())
            worker.join();
    }
    void Reconfigure(unsigned channel, const GatewayCanTiming& timing) {
        if (channel >= initialization.channel_count) {
            throw std::runtime_error("Ethernet gateway has no CAN channel " +
                                     std::to_string(channel));
        }
        ReconfigureRequest request{channel, timing, {}};
        auto completion = request.completion.get_future();
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            if (!running) {
                throw std::runtime_error("Ethernet management is stopping");
            }
            pending_requests.push_back(std::move(request));
        }
        if (completion.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
            throw std::runtime_error("Ethernet management did not service the bus reconfiguration");
        }
        completion.get();
    }

    void ServicePendingRequest() {
        ReconfigureRequest request;
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            if (pending_requests.empty()) {
                return;
            }
            request = std::move(pending_requests.front());
            pending_requests.pop_front();
        }

        try {
            ConfigureCanChannel(*sock, request.channel, request.timing, identity, {});
            request.completion.set_value();
        } catch (...) {
            request.completion.set_exception(std::current_exception());
        }
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
            ServicePendingRequest();
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
            if (!event || event->channel >= initialization.channel_count)
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
                                   const TimingFactory& make_timings,
                                   std::function<bool(unsigned)> has_devices)
    : impl_(std::make_unique<Impl>(endpoint, std::move(logger), make_timings,
                                   std::move(has_devices))) {}

void ManagementClient::ReconfigureChannel(unsigned channel, const GatewayCanTiming& timing) {
    impl_->Reconfigure(channel, timing);
}
const GatewayInitialization& ManagementClient::Initialization() const {
    return impl_->initialization;
}
ManagementClient::~ManagementClient() = default;
}  // namespace encos::ethernet
