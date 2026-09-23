// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "utils/fdBroker/file_capabilities.h"
#ifdef __linux__
#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ethernet/transport/datagram.h"
#include "ethernet/transport/linux/xdp_session.h"
namespace encos::ethernet {
namespace {
struct LocalSocket {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    ~LocalSocket() {
        if (fd >= 0)
            close(fd);
    }
};
}  // namespace
TEST(EthernetXdpSession, RejectsInvalidRequestsWithoutCreatingResources) {
    XdpSession session;
    EXPECT_EQ(session.SocketFd(), -1);
    EXPECT_EQ(session.MemoryFd(), -1);
    encos_xdp_request request{};
    std::fill(std::begin(request.interface_name), std::end(request.interface_name), 'x');
    EXPECT_THROW(session.OpenFile(request, "/nonexistent/filter.bpf.o"), std::runtime_error);
    EXPECT_THROW(session.OpenMemory(request, nullptr, 0), std::invalid_argument);
    EXPECT_EQ(session.SocketFd(), -1);
    EXPECT_EQ(session.MemoryFd(), -1);
}

TEST(EthernetXdpSession, RefusesLoopbackBeforeChangingInterface) {
    EXPECT_THROW(ConfigureXdpInterfaceDirect("lo"), std::runtime_error);
}

TEST(EthernetDatagram, RecoversAfterPeerPortClosesAndReopens) {
    ifaddrs* addresses = nullptr;
    ASSERT_EQ(getifaddrs(&addresses), 0);
    std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> owner(addresses, freeifaddrs);
    Endpoint endpoint;
    sockaddr_in local{};
    for (auto* item = addresses; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
            !(item->ifa_flags & IFF_UP) || !(item->ifa_flags & IFF_BROADCAST) ||
            (item->ifa_flags & IFF_LOOPBACK))
            continue;
        local = *reinterpret_cast<sockaddr_in*>(item->ifa_addr);
        char address[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &local.sin_addr, address, sizeof address);
        endpoint = {item->ifa_name, address, 0};
        break;
    }
    if (endpoint.interface_name.empty())
        GTEST_SKIP() << "No active IPv4 broadcast interface";
    LocalSocket reservation;
    ASSERT_GE(reservation.fd, 0);
    ASSERT_EQ(bind(reservation.fd, reinterpret_cast<sockaddr*>(&local), sizeof local), 0);
    socklen_t length = sizeof local;
    ASSERT_EQ(getsockname(reservation.fd, reinterpret_cast<sockaddr*>(&local), &length), 0);
    endpoint.port = ntohs(local.sin_port);
    auto channel = ConnectDatagram(endpoint);
    close(reservation.fd);
    reservation.fd = -1;
    const uint8_t query[] = {1, 2, 3};
    ASSERT_TRUE(channel->Send(query, sizeof query));
    ASSERT_TRUE(channel->Wait(1000));
    uint8_t reply[16]{};
    EXPECT_NO_THROW(EXPECT_EQ(channel->Receive(reply, sizeof reply), 0U));
    LocalSocket recovered;
    ASSERT_GE(recovered.fd, 0);
    ASSERT_EQ(bind(recovered.fd, reinterpret_cast<sockaddr*>(&local), sizeof local), 0);
    ASSERT_TRUE(channel->Send(query, sizeof query));
    sockaddr_in client{};
    length = sizeof client;
    timeval timeout{1, 0};
    ASSERT_EQ(setsockopt(recovered.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout), 0);
    ASSERT_EQ(recvfrom(recovered.fd, reply, sizeof reply, 0, reinterpret_cast<sockaddr*>(&client),
                       &length),
              3);
    ASSERT_EQ(sendto(recovered.fd, reply, 3, 0, reinterpret_cast<sockaddr*>(&client), length), 3);
    ASSERT_TRUE(channel->Wait(1000));
    EXPECT_EQ(channel->Receive(reply, sizeof reply), 3U);
}
}  // namespace encos::ethernet
#endif

TEST(EthernetCapabilities, RequiresBpfAndEffectiveNetworkCapabilities) {
    const std::vector<cap_value_t> required{CAP_NET_RAW, CAP_NET_ADMIN, CAP_BPF};
    for (const auto* incomplete :
         {"=", "cap_net_raw,cap_net_admin=ep", "cap_net_raw,cap_net_admin,cap_bpf=p"}) {
        cap_t caps = cap_from_text(incomplete);
        ASSERT_NE(caps, nullptr);
        EXPECT_FALSE(encos::fd_broker::HasCapabilities(caps, required));
        cap_free(caps);
    }
    cap_t caps = cap_from_text("cap_net_raw,cap_net_admin,cap_bpf=ep");
    ASSERT_NE(caps, nullptr);
    EXPECT_TRUE(encos::fd_broker::HasCapabilities(caps, required));
    cap_free(caps);
    EXPECT_FALSE(encos::fd_broker::HasCapabilities(nullptr, required));
    EXPECT_FALSE(encos::fd_broker::HasFileCapabilities("/nonexistent/encos-helper", required));
}
