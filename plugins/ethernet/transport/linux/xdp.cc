#include "ethernet/transport/linux/xdp.h"

#include <arpa/inet.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <map>
#include <net/if.h>
#include <net/if_arp.h>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <xdp/xsk.h>

#include "ethernet/ethernet_slave.h"
#include "ethernet/protocol/gateway_payload.h"
#include "utils/fdBroker/file_capabilities.h"
#include "xdp_shared.h"
#ifdef ENCOS_STATIC_MODE
#include "ethernet_xdp_embedded.generated.h"
#include "xdp_session.h"
#endif
extern char** environ;

namespace encos::ethernet {
namespace {
void Require(bool ok, const char* what) {
    if (!ok)
        throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}
void RequireSingleRxQueue(const Endpoint& endpoint) {
    const auto path = std::filesystem::path("/sys/class/net") / endpoint.interface_name / "queues";
    unsigned count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(path))
        if (entry.path().filename().string().rfind("rx-", 0) == 0)
            ++count;
    if (count != 1 || !std::filesystem::exists(path / "rx-0"))
        throw std::runtime_error(
            "Ethernet AF_XDP requires one RX queue on " + endpoint.interface_name +
            "; configure with ethtool -L <interface> combined 1 (or rx 1), then retry");
}
struct Mapping {
    void* data = MAP_FAILED;
    std::size_t size = 0;
    ~Mapping() {
        if (data != MAP_FAILED)
            munmap(data, size);
    }
    void Open(int fd, std::size_t length, off_t offset) {
        size = length;
        data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
        Require(data != MAP_FAILED, "mmap AF_XDP ring");
    }
};
}  // namespace
void ConfigureXdpHostInterface(const std::string& interface_name, const std::string& broker_path) {
    if (interface_name.empty() || interface_name.size() >= IFNAMSIZ ||
        interface_name.find(':') != std::string::npos)
        throw std::invalid_argument("Invalid Ethernet interface name");
#ifdef ENCOS_STATIC_MODE
    (void) broker_path;
    RequireXdpProcessCapabilities();
    ConfigureXdpInterfaceDirect(interface_name.c_str());
#else
    fd_broker::EnsureFileCapabilities(broker_path, {CAP_NET_RAW, CAP_NET_ADMIN, CAP_BPF},
                                      "cap_net_raw,cap_net_admin,cap_bpf+ep");
    std::array<char*, 4> args{const_cast<char*>(broker_path.c_str()),
                              const_cast<char*>("--configure-interface"),
                              const_cast<char*>(interface_name.c_str()), nullptr};
    pid_t child = -1;
    const int result =
        posix_spawn(&child, broker_path.c_str(), nullptr, nullptr, args.data(), environ);
    if (result != 0)
        throw std::runtime_error("Start Ethernet network configuration: " +
                                 std::string(std::strerror(result)));
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error(
            "Ethernet network configuration failed; check broker CAP_NET_ADMIN");
#endif
}
struct XdpTransport::Impl {
    Route route;
    bool multi = false;
    std::map<unsigned, Route> routes;
    std::unordered_map<uint32_t, unsigned> sources;
    int udp = -1, control = -1, socket = -1, memory_fd = -1, wake = -1;
    pid_t child = -1;
#ifdef ENCOS_STATIC_MODE
    std::unique_ptr<XdpSession> session;
#endif
    Mapping memory, rx_map, tx_map, fill_map, completion_map;
    xsk_ring_cons rx{}, completion{};
    xsk_ring_prod tx{}, fill{};
    std::vector<uint64_t> free_tx, pending_fill;
    uint64_t tx_packets = 0, tx_records = 0, rx_packets = 0, rx_records = 0, rx_invalid = 0;
    std::array<uint64_t, 8> tx_controls{}, rx_statuses{};
    ~Impl() {
        if (socket >= 0 && std::getenv("ENCOS_ETHERNET_STATS") != nullptr) {
            xdp_statistics stats{};
            socklen_t size = sizeof(stats);
            const int result = getsockopt(socket, SOL_XDP, XDP_STATISTICS, &stats, &size);
            std::fprintf(stderr,
                         "AF_XDP statistics: tx_packets=%llu tx_records=%llu rx_packets=%llu "
                         "rx_records=%llu invalid_packets=%llu stats_errno=%d "
                         "rx_dropped=%llu rx_ring_full=%llu rx_fill_empty=%llu "
                         "rx_invalid_desc=%llu tx_invalid_desc=%llu\n",
                         static_cast<unsigned long long>(tx_packets),
                         static_cast<unsigned long long>(tx_records),
                         static_cast<unsigned long long>(rx_packets),
                         static_cast<unsigned long long>(rx_records),
                         static_cast<unsigned long long>(rx_invalid), result == 0 ? 0 : errno,
                         static_cast<unsigned long long>(stats.rx_dropped),
                         static_cast<unsigned long long>(stats.rx_ring_full),
                         static_cast<unsigned long long>(stats.rx_fill_ring_empty_descs),
                         static_cast<unsigned long long>(stats.rx_invalid_descs),
                         static_cast<unsigned long long>(stats.tx_invalid_descs));
            for (unsigned bus = 0; bus < 8; bus++)
                std::fprintf(stderr, "AF_XDP CAN%u: control_requests=%llu status_replies=%llu\n",
                             bus, static_cast<unsigned long long>(tx_controls[bus]),
                             static_cast<unsigned long long>(rx_statuses[bus]));
        }
        if (udp >= 0)
            close(udp);
        if (socket >= 0)
            close(socket);
        if (memory_fd >= 0)
            close(memory_fd);
        if (wake >= 0)
            close(wake);
        if (control >= 0)
            close(control);
        if (child > 0) {
            int status = 0;
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        }
    }
    void ConfigureRoute(const Endpoint& endpoint) {
        udp = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        Require(udp >= 0, "UDP discovery socket");
        ifreq interface{};
        std::strncpy(interface.ifr_name, endpoint.interface_name.c_str(), IFNAMSIZ - 1);
        Require(ioctl(udp, SIOCGIFHWADDR, &interface) == 0, "Read interface MAC");
        std::memcpy(route.local_mac.data(), interface.ifr_hwaddr.sa_data, 6);
        Require(ioctl(udp, SIOCGIFADDR, &interface) == 0,
                "Configure a static IPv4 address on the interface first");
        auto local = *reinterpret_cast<sockaddr_in*>(&interface.ifr_addr);
        route.local_ip = local.sin_addr.s_addr;
        local.sin_port = 0;
        Require(bind(udp, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0,
                "Bind local UDP address");
        const auto iface_index = htonl(if_nametoindex(endpoint.interface_name.c_str()));
        Require(setsockopt(udp, IPPROTO_IP, IP_UNICAST_IF, &iface_index, sizeof iface_index) == 0,
                "Select UDP interface");
        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(endpoint.port);
        inet_pton(AF_INET, endpoint.address.c_str(), &remote.sin_addr);
        route.remote_ip = remote.sin_addr.s_addr;
        route.remote_port = endpoint.port;
        Require(connect(udp, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) == 0,
                "UDP route lookup");
        socklen_t length = sizeof(local);
        Require(getsockname(udp, reinterpret_cast<sockaddr*>(&local), &length) == 0,
                "Read local UDP port");
        route.local_port = ntohs(local.sin_port);
        const auto heartbeat = EncodePayload({});
        Require(send(udp, heartbeat.data(), heartbeat.size(), 0) ==
                    static_cast<ssize_t>(heartbeat.size()),
                "Trigger neighbor discovery");
        arpreq arp{};
        std::memcpy(&arp.arp_pa, &remote, sizeof(remote));
        std::strncpy(arp.arp_dev, endpoint.interface_name.c_str(), sizeof(arp.arp_dev) - 1);
        for (unsigned attempt = 0; attempt < 200; attempt++) {
            if (ioctl(udp, SIOCGARP, &arp) == 0 && (arp.arp_flags & ATF_COM)) {
                std::memcpy(route.remote_mac.data(), arp.arp_ha.sa_data, 6);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        throw std::runtime_error(
            "No directly connected neighbor for Ethernet gateway; check interface, subnet and "
            "board link");
    }
    void AddRoute(const Endpoint& endpoint) {
        Route next = route;
        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(endpoint.port);
        inet_pton(AF_INET, endpoint.address.c_str(), &remote.sin_addr);
        next.remote_ip = remote.sin_addr.s_addr;
        next.remote_port = endpoint.port;
        const auto heartbeat = EncodePayload({});
        Require(
            sendto(udp, heartbeat.data(), heartbeat.size(), 0, reinterpret_cast<sockaddr*>(&remote),
                   sizeof remote) == static_cast<ssize_t>(heartbeat.size()),
            "Resolve slave neighbor");
        arpreq arp{};
        std::memcpy(&arp.arp_pa, &remote, sizeof remote);
        std::strncpy(arp.arp_dev, endpoint.interface_name.c_str(), sizeof arp.arp_dev - 1);
        for (unsigned attempt = 0; attempt < 200; ++attempt) {
            if (ioctl(udp, SIOCGARP, &arp) == 0 && (arp.arp_flags & ATF_COM)) {
                std::memcpy(next.remote_mac.data(), arp.arp_ha.sa_data, 6);
                const auto slave = SlaveId(endpoint.address);
                if (!routes.emplace(slave, next).second)
                    throw std::invalid_argument("Duplicate slave ID");
                sources.emplace(next.remote_ip, slave);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        throw std::runtime_error("No neighbor for discovered slave " + endpoint.address);
    }
    void Bootstrap(const Endpoint& endpoint, const std::string& path) {
        encos_xdp_request request{};
        std::strncpy(request.interface_name, endpoint.interface_name.c_str(),
                     sizeof(request.interface_name) - 1);
        request.filter = {route.local_ip, multi ? 0U : route.remote_ip, htons(route.local_port),
                          htons(route.remote_port), 0};
#ifdef ENCOS_STATIC_MODE
        (void) path;
        RequireXdpProcessCapabilities();
        session = std::make_unique<XdpSession>();
        session->OpenMemory(request, kEmbeddedXdpFilter, sizeof(kEmbeddedXdpFilter));
        socket = fcntl(session->SocketFd(), F_DUPFD_CLOEXEC, 0);
        Require(socket >= 0, "Duplicate AF_XDP socket");
        memory_fd = fcntl(session->MemoryFd(), F_DUPFD_CLOEXEC, 0);
        Require(memory_fd >= 0, "Duplicate AF_XDP UMEM descriptor");
#else
        int pair[2];
        Require(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0, "socketpair");
        control = pair[0];
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addclose(&actions, pair[0]);
        posix_spawn_file_actions_adddup2(&actions, pair[1], 3);
        if (pair[1] != 3)
            posix_spawn_file_actions_addclose(&actions, pair[1]);
        std::array<char*, 4> argv{const_cast<char*>(path.c_str()),
                                  const_cast<char*>("--control-fd"), const_cast<char*>("3"),
                                  nullptr};
        // 辅助进程必须在 Ctrl+C 后继续收尾，直到父进程关闭控制连接。
        // 独立进程组避免终端 SIGINT 绕过 Session 析构和 XDP 卸载。
        posix_spawnattr_t attributes;
        int result = posix_spawnattr_init(&attributes);
        if (result == 0) {
            result = posix_spawnattr_setpgroup(&attributes, 0);
            if (result == 0)
                result = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
            if (result == 0)
                result =
                    posix_spawn(&child, path.c_str(), &actions, &attributes, argv.data(), environ);
            posix_spawnattr_destroy(&attributes);
        }
        posix_spawn_file_actions_destroy(&actions);
        close(pair[1]);
        if (result) {
            child = -1;
            errno = result;
            Require(false, "Start EthernetXdpBrokerExecutable");
        }
        Require(send(control, &request, sizeof(request), MSG_NOSIGNAL) == sizeof(request),
                "Send XDP bootstrap configuration");
        pollfd pending{control, POLLIN, 0};
        Require(poll(&pending, 1, 5000) > 0, "AF_XDP initialization timeout");
        encos_xdp_reply reply{};
        iovec io{&reply, sizeof(reply)};
        alignas(cmsghdr) std::array<char, CMSG_SPACE(sizeof(int) * 2)> ancillary{};
        msghdr message{};
        message.msg_iov = &io;
        message.msg_iovlen = 1;
        message.msg_control = ancillary.data();
        message.msg_controllen = ancillary.size();
        const auto n = recvmsg(control, &message, MSG_CMSG_CLOEXEC);
        Require(n == sizeof(reply) && !(message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)),
                "Receive XDP bootstrap reply");
        if (reply.error)
            throw std::runtime_error(
                std::string(reply.message, strnlen(reply.message, sizeof(reply.message))) +
                "; authorize helper: sudo setcap cap_net_raw,cap_net_admin,cap_bpf+ep " + path);
        auto* c = CMSG_FIRSTHDR(&message);
        Require(c && c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS &&
                    c->cmsg_len == CMSG_LEN(sizeof(int) * 2),
                "Receive AF_XDP descriptors");
        std::array<int, 2> fds{};
        std::memcpy(fds.data(), CMSG_DATA(c), sizeof(fds));
        socket = fds[0];
        memory_fd = fds[1];
#endif
    }
    template <class Ring>
    void MapRing(Mapping& mapping, Ring& ring, const xdp_ring_offset& offset, uint32_t count,
                 std::size_t element_size, off_t page) {
        mapping.Open(socket, offset.desc + count * element_size, page);
        auto* base = static_cast<uint8_t*>(mapping.data);
        ring.producer = reinterpret_cast<uint32_t*>(base + offset.producer);
        ring.consumer = reinterpret_cast<uint32_t*>(base + offset.consumer);
        ring.flags = reinterpret_cast<uint32_t*>(base + offset.flags);
        ring.ring = base + offset.desc;
        ring.size = count;
        ring.mask = count - 1;
    }
    void Map() {
        xdp_mmap_offsets offsets{};
        socklen_t length = sizeof(offsets);
        Require(getsockopt(socket, SOL_XDP, XDP_MMAP_OFFSETS, &offsets, &length) == 0,
                "Read XDP ring offsets");
        memory.Open(memory_fd, ENCOS_XDP_UMEM_SIZE, 0);
        MapRing(rx_map, rx, offsets.rx, ENCOS_XDP_RX_SIZE, sizeof(xdp_desc), XDP_PGOFF_RX_RING);
        MapRing(tx_map, tx, offsets.tx, ENCOS_XDP_TX_SIZE, sizeof(xdp_desc), XDP_PGOFF_TX_RING);
        MapRing(fill_map, fill, offsets.fr, ENCOS_XDP_RX_SIZE, sizeof(uint64_t),
                XDP_UMEM_PGOFF_FILL_RING);
        MapRing(completion_map, completion, offsets.cr, ENCOS_XDP_TX_SIZE, sizeof(uint64_t),
                XDP_UMEM_PGOFF_COMPLETION_RING);
        tx.cached_cons = ENCOS_XDP_TX_SIZE;
        fill.cached_cons = ENCOS_XDP_RX_SIZE;
        uint32_t index = 0;
        Require(xsk_ring_prod__reserve(&fill, ENCOS_XDP_RX_SIZE, &index) == ENCOS_XDP_RX_SIZE,
                "Prime XDP fill ring");
        for (uint32_t i = 0; i < ENCOS_XDP_RX_SIZE; i++)
            *xsk_ring_prod__fill_addr(&fill, index + i) = i * ENCOS_XDP_FRAME_SIZE;
        xsk_ring_prod__submit(&fill, ENCOS_XDP_RX_SIZE);
        for (uint64_t i = ENCOS_XDP_RX_SIZE; i < ENCOS_XDP_FRAME_COUNT; i++)
            free_tx.push_back(i * ENCOS_XDP_FRAME_SIZE);
        wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        Require(wake >= 0, "eventfd");
    }
    void Kick() {
        if (xsk_ring_prod__needs_wakeup(&tx)) {
            if (sendto(socket, nullptr, 0, MSG_DONTWAIT, nullptr, 0) < 0 && errno != EAGAIN &&
                errno != EBUSY && errno != ENOBUFS)
                Require(false, "Wake AF_XDP TX");
        }
    }
    void Reap() {
        uint32_t index = 0;
        const auto count = xsk_ring_cons__peek(&completion, ENCOS_XDP_TX_SIZE, &index);
        for (uint32_t i = 0; i < count; i++)
            free_tx.push_back(*xsk_ring_cons__comp_addr(&completion, index + i));
        if (count)
            xsk_ring_cons__release(&completion, count);
    }
};
XdpTransport::XdpTransport(const Endpoint& endpoint, const std::string& broker_path)
    : impl_(std::make_unique<Impl>()) {
    RequireSingleRxQueue(endpoint);
    impl_->ConfigureRoute(endpoint);
    impl_->routes.emplace(0, impl_->route);
    impl_->sources.emplace(impl_->route.remote_ip, 0);
    impl_->Bootstrap(endpoint, broker_path);
    impl_->Map();
}
XdpTransport::XdpTransport(const std::vector<Endpoint>& endpoints, const std::string& broker_path)
    : impl_(std::make_unique<Impl>()) {
    if (endpoints.empty())
        throw std::runtime_error("No Ethernet slaves discovered");
    for (const auto& ep : endpoints) {
        if (ep.interface_name != endpoints.front().interface_name || ep.port != 5000)
            throw std::invalid_argument("Slaves must share one interface and UDP 5000");
        (void) SlaveId(ep.address);
    }
    RequireSingleRxQueue(endpoints.front());
    impl_->multi = true;
    impl_->ConfigureRoute(endpoints.front());
    if ((ntohl(impl_->route.local_ip) & 0xffffff00U) != 0xc0a86400U)
        throw std::invalid_argument("Host interface must use 192.168.100.0/24");
    ifreq netmask{};
    std::strncpy(netmask.ifr_name, endpoints.front().interface_name.c_str(), IFNAMSIZ - 1);
    Require(ioctl(impl_->udp, SIOCGIFNETMASK, &netmask) == 0, "Read interface netmask");
    if (ntohl(reinterpret_cast<sockaddr_in*>(&netmask.ifr_netmask)->sin_addr.s_addr) != 0xffffff00U)
        throw std::invalid_argument("Host interface requires netmask 255.255.255.0");
    for (const auto& ep : endpoints)
        impl_->AddRoute(ep);
    impl_->Bootstrap(endpoints.front(), broker_path);
    impl_->Map();
}
XdpTransport::~XdpTransport() = default;
bool XdpTransport::Send(const MotorMessages& messages) {
    return Send(0, messages);
}
bool XdpTransport::Send(unsigned slave, const MotorMessages& messages) {
    auto& p = *impl_;
    p.Reap();
    const auto bytes = EncodePacket(p.routes.at(slave), EncodePayload(messages));
    uint32_t index = 0;
    if (p.free_tx.empty() || xsk_ring_prod__reserve(&p.tx, 1, &index) != 1) {
        p.Kick();
        return false;
    }
    const auto address = p.free_tx.back();
    p.free_tx.pop_back();
    std::memcpy(static_cast<uint8_t*>(p.memory.data) + address, bytes.data(), bytes.size());
    auto* descriptor = xsk_ring_prod__tx_desc(&p.tx, index);
    descriptor->addr = address;
    descriptor->len = static_cast<uint32_t>(bytes.size());
    descriptor->options = 0;
    xsk_ring_prod__submit(&p.tx, 1);
    p.tx_packets++;
    p.tx_records += messages.size();
    for (const auto& message : messages)
        if (message.bus_idx >= 0 && message.bus_idx < 8 && message.data.len == 8 &&
            (message.data.data[0] & 0xe0) == 0) {
            if ((p.tx_controls[message.bus_idx] == 0 || p.tx_controls[message.bus_idx] == 1000) &&
                std::getenv("ENCOS_ETHERNET_STATS") != nullptr) {
                std::fprintf(stderr, "AF_XDP control #%llu CAN%d id=%u flags=%u data=",
                             static_cast<unsigned long long>(p.tx_controls[message.bus_idx]),
                             message.bus_idx, message.data.id, message.data.frame_flags);
                for (const auto byte : message.data.data)
                    std::fprintf(stderr, "%02x", byte);
                std::fprintf(stderr, "\n");
            }
            p.tx_controls[message.bus_idx]++;
        }
    p.Kick();
    return true;
}
MotorMessages XdpTransport::Receive() {
    auto& p = *impl_;
    p.Reap();
    p.Kick();
    uint32_t index = 0;
    const auto count = xsk_ring_cons__peek(&p.rx, 64, &index);
    MotorMessages result;
    for (uint32_t i = 0; i < count; i++) {
        const auto* desc = xsk_ring_cons__rx_desc(&p.rx, index + i);
        if (desc->addr + desc->len > ENCOS_XDP_UMEM_SIZE)
            throw std::runtime_error("Invalid AF_XDP RX descriptor");
        const auto* data = static_cast<uint8_t*>(p.memory.data) + desc->addr;
        uint32_t source = 0;
        if (desc->len >= 30)
            std::memcpy(&source, data + 26, 4);
        const auto peer = p.sources.find(source);
        auto messages = peer == p.sources.end()
                            ? std::optional<MotorMessages>{}
                            : DecodePacket(p.routes.at(peer->second), data, desc->len);
        p.rx_packets++;
        if (messages) {
            p.rx_records += messages->size();
            for (const auto& message : *messages)
                if (message.bus_idx >= 0 && message.bus_idx < 8 && message.data.len == 8 &&
                    (message.data.data[0] & 0xe0) == 0x20)
                    p.rx_statuses[message.bus_idx]++;
            if (p.multi)
                for (auto& message : *messages)
                    message.bus_idx |= static_cast<int>(peer->second << 16);
            result.insert(result.end(), messages->begin(), messages->end());
        } else {
            p.rx_invalid++;
            if (p.rx_invalid <= 3 && std::getenv("ENCOS_ETHERNET_STATS") != nullptr) {
                std::fprintf(stderr, "AF_XDP invalid packet (%u bytes): ", desc->len);
                const auto* bytes = static_cast<uint8_t*>(p.memory.data) + desc->addr;
                for (uint32_t n = 0; n < desc->len; n++)
                    std::fprintf(stderr, "%02x", bytes[n]);
                std::fprintf(stderr, "\n");
            }
        }
        p.pending_fill.push_back(desc->addr & ~static_cast<uint64_t>(ENCOS_XDP_FRAME_SIZE - 1));
    }
    if (count)
        xsk_ring_cons__release(&p.rx, count);
    while (!p.pending_fill.empty() && xsk_ring_prod__reserve(&p.fill, 1, &index) == 1) {
        *xsk_ring_prod__fill_addr(&p.fill, index) = p.pending_fill.back();
        p.pending_fill.pop_back();
        xsk_ring_prod__submit(&p.fill, 1);
    }
    return result;
}
void XdpTransport::Wait(int milliseconds) {
    auto& p = *impl_;
    pollfd descriptors[2] = {{p.socket, POLLIN, 0}, {p.wake, POLLIN, 0}};
    const int result = poll(descriptors, 2, milliseconds);
    if (result < 0 && errno != EINTR)
        Require(false, "Poll AF_XDP");
    if (descriptors[1].revents & POLLIN) {
        uint64_t value = 0;
        (void) read(p.wake, &value, sizeof(value));
    }
}
void XdpTransport::Notify() {
    const uint64_t one = 1;
    (void) write(impl_->wake, &one, sizeof(one));
}
}  // namespace encos::ethernet
