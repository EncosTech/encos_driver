// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "host_network.h"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ifaddrs.h>
#include <linux/rtnetlink.h>
#include <memory>
#include <net/if.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

#include "ethernet/transport/transport.h"
#include "network_manager.h"
#include "xdp.h"

namespace encos::ethernet {
HostNetworkAction SelectHostNetworkAction(const HostNetworkState& state) {
    if (state.configured && (!state.managed || state.static_profile))
        return HostNetworkAction::Reuse;
    if (state.occupied)
        throw std::runtime_error("Interface already carries another connection");
    if (state.managed) {
        if (!state.available_for_activation)
            throw std::runtime_error(
                "NetworkManager device cannot activate a temporary static connection in its "
                "current state");
        return HostNetworkAction::ActivateNetworkManager;
    }
    if (state.exclusive)
        return HostNetworkAction::ConfigureDirect;
    throw std::runtime_error(
        "Configure 192.168.100.254/24 with the system network service, or set "
        "host_network_exclusive for a dedicated unmanaged interface");
}

namespace {
constexpr uint32_t kMotorHost = 0xc0a864feU;
constexpr uint32_t kMotorNetmask = 0xffffff00U;

struct InterfaceState {
    bool configured = false;
    bool occupied = false;
    bool other_ipv4 = false;
    bool ssh_subnet = false;
    bool link_ready = false;
};

struct RouteState {
    unsigned interface_index = 0;
    uint32_t source = 0;
    bool attached = false;
    bool gateway = false;
};

struct RouteRequest {
    struct Attribute {
        rtattr header{};
        uint32_t value{};
    };

    nlmsghdr header{};
    rtmsg route{};
    Attribute destination{};
    Attribute source{};
    Attribute output{};
};
constexpr auto kAddressLength = RTA_SPACE(sizeof(uint32_t));
static_assert(sizeof(RouteRequest) == NLMSG_LENGTH(sizeof(rtmsg)) + 3 * kAddressLength);

class RouteSocket {
public:
    RouteSocket() : fd_(socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE)) {
        if (fd_ < 0)
            Fail("Open kernel route socket");
        sockaddr_nl kernel{};
        kernel.nl_family = AF_NETLINK;
        const timeval timeout{2, 0};
        if (connect(fd_, reinterpret_cast<sockaddr*>(&kernel), sizeof(kernel)) < 0 ||
            setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            const int error = errno;
            close(fd_);
            throw std::runtime_error("Configure kernel route socket: " +
                                     std::string(std::strerror(error)));
        }
    }
    ~RouteSocket() {
        close(fd_);
    }
    RouteSocket(const RouteSocket&) = delete;
    RouteSocket& operator=(const RouteSocket&) = delete;

    template <typename Visitor>
    void Query(uint32_t address, uint32_t source, unsigned interface_index, bool dump,
               Visitor visit) const {
        RouteRequest request;
        request.header.nlmsg_len = NLMSG_LENGTH(sizeof(rtmsg));
        request.header.nlmsg_type = RTM_GETROUTE;
        request.header.nlmsg_flags = NLM_F_REQUEST | (dump ? NLM_F_DUMP : 0);
        request.header.nlmsg_seq = 1;
        request.route.rtm_family = dump ? AF_UNSPEC : AF_INET;
        if (!dump) {
            request.header.nlmsg_len = sizeof(request);
            request.route.rtm_dst_len = 32;
            request.route.rtm_src_len = 32;
            request.route.rtm_flags = RTM_F_LOOKUP_TABLE;
            request.destination = {{RTA_LENGTH(sizeof(address)), RTA_DST}, address};
            request.source = {{RTA_LENGTH(sizeof(source)), RTA_SRC}, source};
            request.output = {{RTA_LENGTH(sizeof(interface_index)), RTA_OIF}, interface_index};
        }
        const auto length = request.header.nlmsg_len;
        if (send(fd_, &request, length, 0) != static_cast<ssize_t>(length))
            Fail("Request kernel route");
        alignas(nlmsghdr) std::array<char, 32768> buffer{};
        for (;;) {
            auto received = recv(fd_, buffer.data(), buffer.size(), MSG_TRUNC);
            if (received < 0 && errno == EINTR)
                continue;
            if (received <= 0)
                Fail("Read kernel route information");
            if (received > static_cast<ssize_t>(buffer.size()))
                throw std::runtime_error("Kernel route response was truncated");
            auto remaining = static_cast<int>(received);
            for (auto* message = reinterpret_cast<nlmsghdr*>(buffer.data());
                 NLMSG_OK(message, remaining); message = NLMSG_NEXT(message, remaining)) {
                if (message->nlmsg_seq != request.header.nlmsg_seq)
                    continue;
                if (message->nlmsg_flags & NLM_F_DUMP_INTR)
                    throw std::runtime_error("Kernel routes changed during Ethernet setup; retry");
                if (message->nlmsg_type == NLMSG_DONE)
                    return;
                if (message->nlmsg_type == NLMSG_ERROR) {
                    const auto* error = static_cast<const nlmsgerr*>(NLMSG_DATA(message));
                    if (error->error != 0)
                        throw std::runtime_error("Kernel route lookup: " +
                                                 std::string(std::strerror(-error->error)));
                    return;
                }
                if (message->nlmsg_type == RTM_NEWROUTE &&
                    (visit(static_cast<const rtmsg*>(NLMSG_DATA(message)),
                           static_cast<int>(RTM_PAYLOAD(message))) ||
                     !dump))
                    return;
            }
        }
    }

private:
    [[noreturn]] static void Fail(const char* operation) {
        throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
    }
    int fd_;
};

template <typename T>
T RouteValue(const rtattr* attribute) {
    T value{};
    if (RTA_PAYLOAD(attribute) >= sizeof(value))
        std::memcpy(&value, RTA_DATA(attribute), sizeof(value));
    return value;
}

bool CarriesDefaultRoute(unsigned index) {
    bool found = false;
    RouteSocket().Query(0, 0, 0, true, [&](const rtmsg* route, int remaining) {
        if (route->rtm_dst_len != 0 || route->rtm_type != RTN_UNICAST)
            return false;
        for (auto* attribute = RTM_RTA(route); RTA_OK(attribute, remaining);
             attribute = RTA_NEXT(attribute, remaining)) {
            if (attribute->rta_type == RTA_OIF && RouteValue<unsigned>(attribute) == index)
                return found = true;
            if (attribute->rta_type == RTA_MULTIPATH) {
                auto length = static_cast<int>(RTA_PAYLOAD(attribute));
                auto* hop = static_cast<rtnexthop*>(RTA_DATA(attribute));
                for (; RTNH_OK(hop, length);
                     length -= RTNH_ALIGN(hop->rtnh_len), hop = RTNH_NEXT(hop)) {
                    if (static_cast<unsigned>(hop->rtnh_ifindex) == index)
                        return found = true;
                }
            }
        }
        return false;
    });
    return found;
}

struct SshAddresses {
    std::string peer;
    std::string local;
};

SshAddresses ReadSshAddresses() {
    SshAddresses addresses;
    if (const auto* ssh = std::getenv("SSH_CONNECTION")) {
        std::istringstream connection(ssh);
        std::string peer_port;
        connection >> addresses.peer >> peer_port >> addresses.local;
    }
    return addresses;
}

bool ParseSshIpv4(const std::string& text, in_addr& address) {
    if (inet_pton(AF_INET, text.c_str(), &address) == 1)
        return true;
    in6_addr ipv6{};
    if (inet_pton(AF_INET6, text.c_str(), &ipv6) != 1 || !IN6_IS_ADDR_V4MAPPED(&ipv6))
        return false;
    std::memcpy(&address, ipv6.s6_addr + 12, sizeof(address));
    return true;
}

InterfaceState InspectInterface(const std::string& name) {
    ifaddrs* first = nullptr;
    if (getifaddrs(&first) < 0)
        throw std::runtime_error("Cannot read addresses on " + name);
    const std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> addresses(first, freeifaddrs);
    const auto ssh = ReadSshAddresses();
    const auto local_ssh = ssh.local.substr(0, ssh.local.find('%'));
    in_addr ssh_ipv4{}, ssh_peer{};
    in6_addr ssh_ipv6{};
    const bool ssh4 = ParseSshIpv4(local_ssh, ssh_ipv4);
    const bool ssh6 = inet_pton(AF_INET6, local_ssh.c_str(), &ssh_ipv6) == 1;

    InterfaceState state;
    state.ssh_subnet = ParseSshIpv4(ssh.peer, ssh_peer) &&
                       (ntohl(ssh_peer.s_addr) & kMotorNetmask) == (kMotorHost & kMotorNetmask);
    for (const auto* current = first; current; current = current->ifa_next) {
        if (name != current->ifa_name)
            continue;
        if (current->ifa_flags & IFF_LOOPBACK)
            throw std::invalid_argument("Cannot configure Ethernet on loopback " + name);
        state.link_ready = (current->ifa_flags & IFF_UP) && (current->ifa_flags & IFF_RUNNING);
        if (!current->ifa_addr)
            continue;
        if (current->ifa_addr->sa_family == AF_INET) {
            const auto address =
                reinterpret_cast<const sockaddr_in*>(current->ifa_addr)->sin_addr.s_addr;
            const auto mask =
                current->ifa_netmask
                    ? reinterpret_cast<const sockaddr_in*>(current->ifa_netmask)->sin_addr.s_addr
                    : 0U;
            const bool motor = ntohl(address) == kMotorHost && ntohl(mask) == kMotorNetmask;
            state.configured |= motor;
            state.other_ipv4 |= !motor;
            state.occupied |= !motor || (ssh4 && address == ssh_ipv4.s_addr);
        } else if (current->ifa_addr->sa_family == AF_INET6) {
            const auto& address =
                reinterpret_cast<const sockaddr_in6*>(current->ifa_addr)->sin6_addr;
            state.occupied |= !IN6_IS_ADDR_LINKLOCAL(&address) ||
                              (ssh6 && std::memcmp(&address, &ssh_ipv6, sizeof(address)) == 0);
        }
    }
    return state;
}

class LinuxHostInterface final : public HostInterface {
public:
    LinuxHostInterface(std::string name, unsigned index, const std::string& broker, bool exclusive)
        : name_(std::move(name)), index_(index), manager_(name_) {
        const auto state = InspectInterface(name_);
        const bool managed = manager_.Managed();
        try {
            const auto method = managed ? manager_.Ipv4Method() : NetworkIpv4Method::Unavailable;
            const auto action = SelectHostNetworkAction(
                {managed, method == NetworkIpv4Method::Manual, state.configured,
                 managed && manager_.AvailableForActivation(method), state.occupied, exclusive});
            if (action == HostNetworkAction::ActivateNetworkManager)
                manager_.Activate(name_, [this] {
                    CheckTakeover();
                });
            else if (action == HostNetworkAction::ConfigureDirect) {
                CheckTakeover();
                ConfigureXdpHostInterface(name_, broker);
            }
            const auto activated = InspectInterface(name_);
            if (!activated.configured)
                throw std::runtime_error("Expected address 192.168.100.254/24 on the interface");
            if (activated.other_ipv4)
                throw std::runtime_error(
                    "The dedicated interface must use only 192.168.100.254/24");
            if (!activated.link_ready)
                throw std::runtime_error("Ethernet link is down");
        } catch (const std::runtime_error& error) {
            throw std::runtime_error("Ethernet " + name_ + ": " + error.what());
        }
    }

    void ValidateRoute(const std::string& destination) const override {
        in_addr address{};
        if (inet_pton(AF_INET, destination.c_str(), &address) != 1)
            throw std::invalid_argument("Invalid Ethernet IPv4 destination " + destination);
        CheckRoute(address.s_addr, destination);
    }

private:
    void CheckRoute(uint32_t address, const std::string& destination) const {
        RouteState route;
        route.source = htonl(kMotorHost);
        const bool broadcast = address == htonl(INADDR_BROADCAST);
        try {
            RouteSocket().Query(
                address, route.source, index_, false, [&](const rtmsg* entry, int remaining) {
                    // 指定出口时，内核可以在无匹配路由的情况下合成链路路由。
                    route.attached = broadcast ? entry->rtm_type == RTN_BROADCAST
                                               : entry->rtm_type == RTN_UNICAST &&
                                                     entry->rtm_table != RT_TABLE_UNSPEC;
                    for (auto* attribute = RTM_RTA(entry); RTA_OK(attribute, remaining);
                         attribute = RTA_NEXT(attribute, remaining)) {
                        if (attribute->rta_type == RTA_OIF)
                            route.interface_index = RouteValue<unsigned>(attribute);
                        else if (attribute->rta_type == RTA_PREFSRC)
                            route.source = RouteValue<uint32_t>(attribute);
                        else if (attribute->rta_type == RTA_GATEWAY ||
                                 attribute->rta_type == RTA_VIA)
                            route.gateway = true;
                    }
                    return true;
                });
        } catch (const std::runtime_error& error) {
            throw std::runtime_error("Ethernet route to " + destination + " on " + name_ + ": " +
                                     error.what());
        }
        if (!route.attached || route.interface_index != index_ ||
            ntohl(route.source) != kMotorHost || route.gateway) {
            char actual[IFNAMSIZ]{};
            const auto* actual_name = if_indextoname(route.interface_index, actual);
            throw std::runtime_error("Ethernet route to " + destination +
                                     " must be directly attached to " + name_ +
                                     " with source 192.168.100.254; actual interface " +
                                     (actual_name ? actual_name : "(unknown)"));
        }
    }

    void CheckTakeover() const {
        if (if_nametoindex(name_.c_str()) != index_)
            throw std::runtime_error("Interface changed before configuration");
        const auto state = InspectInterface(name_);
        if (state.ssh_subnet)
            throw std::runtime_error("The SSH peer is in the motor subnet; use another SSH route");
        if (state.occupied)
            throw std::runtime_error("Interface already carries another connection");
        if (CarriesDefaultRoute(index_))
            throw std::runtime_error("Interface carries a default route");
    }

    std::string name_;
    unsigned index_;
    NetworkManager manager_;
};
}  // namespace

std::unique_ptr<HostInterface> PrepareLinuxHostInterface(const std::string& name,
                                                         const std::string& broker,
                                                         bool exclusive) {
    if (name.empty() || name.size() >= IFNAMSIZ || name.find(':') != std::string::npos)
        throw std::invalid_argument("Invalid Ethernet interface name");
    const auto index = if_nametoindex(name.c_str());
    if (!index)
        throw std::invalid_argument("Ethernet interface not found: " + name);
    if (name == "lo")
        throw std::invalid_argument("Cannot configure Ethernet on loopback " + name);
    return std::make_unique<LinuxHostInterface>(name, index, broker, exclusive);
}
}  // namespace encos::ethernet
