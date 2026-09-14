#include "ethernet/transport/datagram.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <sched.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
namespace encos::ethernet {
namespace {
bool RetryableDatagramError(int error) {
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR || error == ENOBUFS ||
           error == ECONNREFUSED || error == ECONNRESET || error == ENETUNREACH ||
           error == EHOSTUNREACH || error == ENETDOWN || error == EHOSTDOWN || error == ENETRESET ||
           error == ETIMEDOUT;
}
struct Socket {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    Socket() {
        if (fd < 0)
            throw std::runtime_error("management socket creation failed");
    }
    ~Socket() {
        if (fd >= 0)
            close(fd);
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
};
struct Interface {
    std::string name;
    in_addr address{};
    unsigned index = 0;
};
std::vector<Interface> Interfaces() {
    ifaddrs* raw = nullptr;
    if (getifaddrs(&raw) != 0)
        throw std::runtime_error("getifaddrs failed");
    std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> owner(raw, freeifaddrs);
    std::vector<Interface> result;
    for (auto* item = raw; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
            !(item->ifa_flags & IFF_UP) || !(item->ifa_flags & IFF_BROADCAST) ||
            (item->ifa_flags & IFF_LOOPBACK))
            continue;
        result.push_back({item->ifa_name, reinterpret_cast<sockaddr_in*>(item->ifa_addr)->sin_addr,
                          if_nametoindex(item->ifa_name)});
    }
    return result;
}
class ConnectedSocket final : public DatagramChannel {
    Socket sock_;

public:
    explicit ConnectedSocket(const Endpoint& endpoint) {
        sockaddr_in local{};
        local.sin_family = AF_INET;
        unsigned index = 0;
        for (const auto& iface : Interfaces()) {
            if (iface.name == endpoint.interface_name) {
                local.sin_addr = iface.address;
                index = iface.index;
                break;
            }
        }
        if (!index || bind(sock_.fd, reinterpret_cast<sockaddr*>(&local), sizeof local) != 0)
            throw std::runtime_error("UDP interface bind failed");
        const auto network_index = htonl(index);
        if (setsockopt(sock_.fd, IPPROTO_IP, IP_UNICAST_IF, &network_index, sizeof network_index) !=
            0)
            throw std::runtime_error("UDP interface routing setup failed");
        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(endpoint.port);
        if (inet_pton(AF_INET, endpoint.address.c_str(), &remote.sin_addr) != 1 ||
            connect(sock_.fd, reinterpret_cast<sockaddr*>(&remote), sizeof remote) != 0)
            throw std::runtime_error("UDP peer setup failed");
    }
    bool Send(const uint8_t* data, std::size_t size) override {
        const auto sent = send(sock_.fd, data, size, 0);
        if (sent == static_cast<ssize_t>(size))
            return true;
        if (sent < 0 && RetryableDatagramError(errno))
            return false;
        throw std::runtime_error("UDP send failed: " + std::to_string(errno));
    }
    std::size_t Receive(uint8_t* data, std::size_t capacity) override {
        const auto size = recv(sock_.fd, data, capacity, MSG_TRUNC);
        if (size >= 0)
            return static_cast<std::size_t>(size) <= capacity ? static_cast<std::size_t>(size) : 0;
        if (RetryableDatagramError(errno))
            return 0;
        throw std::runtime_error("UDP receive failed: " + std::to_string(errno));
    }
    bool Wait(int milliseconds) override {
        pollfd wait{sock_.fd, POLLIN, 0};
        return poll(&wait, 1, milliseconds) > 0;
    }
};
class BroadcastSocket final : public DiscoveryChannel {
    Socket sock;
    std::vector<Interface> interfaces;

public:
    explicit BroadcastSocket(const std::string& interface_name) {
        int enabled = 1;
        if (setsockopt(sock.fd, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof enabled) != 0 ||
            setsockopt(sock.fd, IPPROTO_IP, IP_PKTINFO, &enabled, sizeof enabled) != 0)
            throw std::runtime_error("management broadcast setup failed");
        sockaddr_in local{};
        local.sin_family = AF_INET;
        if (bind(sock.fd, reinterpret_cast<sockaddr*>(&local), sizeof local) != 0)
            throw std::runtime_error("discovery bind failed");
        interfaces = Interfaces();
        if (!interface_name.empty()) {
            interfaces.erase(std::remove_if(interfaces.begin(), interfaces.end(),
                                            [&](const auto& iface) {
                                                return iface.name != interface_name;
                                            }),
                             interfaces.end());
            if (interfaces.empty())
                throw std::invalid_argument("No active IPv4 broadcast interface: " +
                                            interface_name);
        }
    }
    void Broadcast(const uint8_t* data, std::size_t size) override {
        for (const auto& iface : interfaces) {
            sockaddr_in target{};
            target.sin_family = AF_INET;
            target.sin_port = htons(5001);
            target.sin_addr.s_addr = INADDR_BROADCAST;
            iovec vec{const_cast<uint8_t*>(data), size};
            alignas(cmsghdr) char control[CMSG_SPACE(sizeof(in_pktinfo))]{};
            msghdr msg{};
            msg.msg_name = &target;
            msg.msg_namelen = sizeof target;
            msg.msg_iov = &vec;
            msg.msg_iovlen = 1;
            msg.msg_control = control;
            msg.msg_controllen = sizeof control;
            auto* header = CMSG_FIRSTHDR(&msg);
            header->cmsg_level = IPPROTO_IP;
            header->cmsg_type = IP_PKTINFO;
            header->cmsg_len = CMSG_LEN(sizeof(in_pktinfo));
            auto* info = reinterpret_cast<in_pktinfo*>(CMSG_DATA(header));
            info->ipi_ifindex = static_cast<int>(iface.index);
            info->ipi_spec_dst = iface.address;
            (void) sendmsg(sock.fd, &msg, 0);
        }
    }
    DiscoveryDatagram Receive(int milliseconds) override {
        pollfd wait{sock.fd, POLLIN, 0};
        if (poll(&wait, 1, milliseconds) <= 0)
            return {};
        std::array<uint8_t, 65> packet{};
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(in_pktinfo))]{};
        sockaddr_in source{};
        iovec vec{packet.data(), packet.size()};
        msghdr msg{};
        msg.msg_name = &source;
        msg.msg_namelen = sizeof source;
        msg.msg_iov = &vec;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof control;
        const auto n = recvmsg(sock.fd, &msg, 0);
        if (n <= 0 || (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)))
            return {};
        unsigned index = 0;
        for (auto* c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c))
            if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_PKTINFO &&
                c->cmsg_len >= CMSG_LEN(sizeof(in_pktinfo)))
                index = reinterpret_cast<in_pktinfo*>(CMSG_DATA(c))->ipi_ifindex;
        char name[IF_NAMESIZE]{}, ip[INET_ADDRSTRLEN]{};
        if (!if_indextoname(index, name) || !inet_ntop(AF_INET, &source.sin_addr, ip, sizeof ip))
            return {};
        if (std::none_of(interfaces.begin(), interfaces.end(), [&](const auto& iface) {
                return iface.index == index;
            }))
            return {};
        return {name, ip, ntohs(source.sin_port), {packet.begin(), packet.begin() + n}};
    }
};
}  // namespace
std::unique_ptr<DatagramChannel> ConnectDatagram(const Endpoint& endpoint) {
    return std::make_unique<ConnectedSocket>(endpoint);
}
std::unique_ptr<DiscoveryChannel> OpenDiscovery(const std::string& interface_name) {
    return std::make_unique<BroadcastSocket>(interface_name);
}
bool SetManagementThreadScheduling() {
    sched_param normal{};
    return sched_setscheduler(0, SCHED_OTHER, &normal) == 0;
}
}  // namespace encos::ethernet
