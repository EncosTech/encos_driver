#include "ethernet/protocol/gateway_payload.h"
#ifdef _WIN32
// clang-format off
#include "ethernet/transport/windows/socket.h"
// clang-format on
#include <algorithm>
#include <future>
#include <gtest/gtest.h>
#include <thread>

#include "ethernet/transport/datagram.h"
#include "ethernet/transport/transport.h"
#include "platform/os.h"
namespace encos::ethernet {
namespace {
class LocalGateway {
public:
    WindowsSocket socket;
    Endpoint endpoint;
    explicit LocalGateway(const WindowsInterface& iface) {
        socket.Bind(iface);
        sockaddr_in local{};
        int length = sizeof local;
        if (getsockname(socket.Get(), reinterpret_cast<sockaddr*>(&local), &length) != 0)
            SocketError("getsockname");
        char address[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &iface.address, address, sizeof address);
        endpoint = {iface.name, address, ntohs(local.sin_port)};
    }
    std::vector<uint8_t> Receive(sockaddr_in& client) {
        WSAPOLLFD wait{socket.Get(), POLLRDNORM, 0};
        if (WSAPoll(&wait, 1, 2000) <= 0)
            return {};
        std::vector<uint8_t> packet(65507);
        int length = sizeof client;
        const auto size = recvfrom(socket.Get(), reinterpret_cast<char*>(packet.data()),
                                   static_cast<int>(packet.size()), 0,
                                   reinterpret_cast<sockaddr*>(&client), &length);
        if (size < 0)
            SocketError("test recvfrom");
        packet.resize(static_cast<std::size_t>(size));
        return packet;
    }
    void Reply(const sockaddr_in& client, const std::vector<uint8_t>& packet) {
        ASSERT_EQ(sendto(socket.Get(), reinterpret_cast<const char*>(packet.data()),
                         static_cast<int>(packet.size()), 0,
                         reinterpret_cast<const sockaddr*>(&client), sizeof client),
                  static_cast<int>(packet.size()));
    }
};
}  // namespace
TEST(EthernetWindows, UsesPlatformInterfaceNamesAndFiltersConnectedPeer) {
    const auto interfaces = WindowsInterfaces();
    if (interfaces.empty())
        GTEST_SKIP() << "No active wired IPv4 adapter";
    const auto names = platform::GetWiredInterfaceNames();
    EXPECT_NE(std::find(names.begin(), names.end(), interfaces.front().name), names.end());
    LocalGateway gateway(interfaces.front()), foreign(interfaces.front());
    auto client = ConnectDatagram(gateway.endpoint);
    const auto payload = EncodePayload({});
    ASSERT_TRUE(client->Send(payload.data(), payload.size()));
    sockaddr_in source{};
    EXPECT_EQ(gateway.Receive(source), payload);
    foreign.Reply(source, payload);
    std::array<uint8_t, 65> buffer{};
    client->Wait(30);
    EXPECT_EQ(client->Receive(buffer.data(), buffer.size()), 0U);
    gateway.Reply(source, payload);
    ASSERT_TRUE(client->Wait(1000));
    EXPECT_EQ(client->Receive(buffer.data(), buffer.size()), payload.size());
    gateway.Reply(source, std::vector<uint8_t>(100, 42));
    ASSERT_TRUE(client->Wait(1000));
    EXPECT_EQ(client->Receive(buffer.data(), buffer.size()), 0U);
}
TEST(EthernetWindows, DataTransportDecodesRepliesAndWakesForReceiveAndNotify) {
    const auto interfaces = WindowsInterfaces();
    if (interfaces.empty())
        GTEST_SKIP() << "No active wired IPv4 adapter";
    LocalGateway gateway(interfaces.front());
    auto transport = CreateTransport({gateway.endpoint}, false);
    MotorMessage message{};
    message.bus_idx = 3;
    message.data.id = 42;
    message.data.len = 1;
    message.data.data[0] = 17;
    ASSERT_TRUE(transport->Send(0, {message}));
    sockaddr_in client{};
    auto packet = gateway.Receive(client);
    ASSERT_EQ(packet, EncodePayload({message}));
    packet[4] = 2;
    auto waiter = std::async(std::launch::async, [&] {
        transport->Wait(2000);
    });
    gateway.Reply(client, packet);
    EXPECT_EQ(waiter.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    waiter.get();
    const auto messages = transport->Receive();
    ASSERT_EQ(messages.size(), 1U);
    EXPECT_EQ(messages[0].bus_idx, 3);
    EXPECT_EQ(messages[0].data.id, 42U);
    EXPECT_EQ(messages[0].data.data[0], 17);
    auto notified = std::async(std::launch::async, [&] {
        transport->Wait(2000);
    });
    transport->Notify();
    EXPECT_EQ(notified.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    notified.get();
}

TEST(EthernetWindows, ClosedPeerPortDoesNotInvalidateSocket) {
    const auto interfaces = WindowsInterfaces();
    if (interfaces.empty())
        GTEST_SKIP() << "No active wired IPv4 adapter";
    auto gateway = std::make_unique<LocalGateway>(interfaces.front());
    auto client = ConnectDatagram(gateway->endpoint);
    gateway.reset();
    const auto payload = EncodePayload({});
    ASSERT_TRUE(client->Send(payload.data(), payload.size()));
    client->Wait(1000);
    std::array<uint8_t, 65> buffer{};
    EXPECT_NO_THROW(EXPECT_EQ(client->Receive(buffer.data(), buffer.size()), 0U));
    EXPECT_NO_THROW((void) client->Send(payload.data(), payload.size()));
}
}  // namespace encos::ethernet
#endif
