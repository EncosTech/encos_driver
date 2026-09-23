// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <cstring>
#include <gtest/gtest.h>

#include "ethernet/ethernet_tx_queue.h"
#include "ethernet/management_heartbeat.h"
#include "ethernet/protocol/gateway_payload.h"
#include "ethernet/protocol/ipv4.h"

namespace encos::ethernet {
TEST(EthernetProtocol, ParsesEndpointAndRejectsAmbiguousLinks) {
    const auto ep = ParseEndpoint("eth0@192.168.100.10,5001");
    EXPECT_EQ(ep.interface_name, "eth0");
    EXPECT_EQ(ep.port, 5001);
    EXPECT_EQ(ParseEndpoint("eth0@192.168.100.10").port, 5000);
    for (const auto* invalid : {"", "eth0", "@1.2.3.4", "eth0@host", "eth0@1.2.3.4,0",
                                "eth0@1.2.3.4,65536", "eth0@1.2.3.4,12x"})
        EXPECT_THROW(ParseEndpoint(invalid), std::invalid_argument);
}
TEST(EthernetProtocol, AcceptsWindowsInterfaceNamesAndStrictIPv4) {
    const auto endpoint = ParseEndpoint("以太网 USB Ethernet Adapter@192.168.100.11");
    EXPECT_EQ(endpoint.interface_name, "以太网 USB Ethernet Adapter");
    const auto bytes = ParseIPv4("192.168.100.11");
    EXPECT_EQ(FormatIPv4(bytes.data()), "192.168.100.11");
    for (const auto* invalid :
         {"1.2.3", "1.2.3.4.5", "01.2.3.4", "1..3.4", "1.2.3.256", "1.2.3.-1", "1.2.3.4 "}) {
        EXPECT_THROW(ParseIPv4(invalid), std::invalid_argument);
    }
    EXPECT_THROW(ParseIPv4(std::string("1.2.3.4\0x", 9)), std::invalid_argument);
}
TEST(EthernetProtocol, MatchesWsWireFormatAndChecksBounds) {
    MotorMessage message{};
    message.bus_idx = 4;
    message.data.id = 0x123;
    message.data.len = 2;
    message.data.data[0] = 0xe0;
    message.data.data[1] = 0x16;
    const auto bytes = EncodePayload({message});
    const std::vector<uint8_t> golden = {'E', 'M', 'R', '1', 1, 1,    0,    0, 4, 0, 0, 0, 0x23,
                                         1,   0,   0,   0,   2, 0xe0, 0x16, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ(bytes, golden);
    EXPECT_EQ(EncodePayload(MotorMessages(80, message)).size(), 1448u);
    EXPECT_THROW(EncodePayload(MotorMessages(81, message)), std::invalid_argument);
    message.bus_idx = 8;
    EXPECT_THROW(EncodePayload({message}), std::invalid_argument);
    auto reply = bytes;
    reply[4] = 2;
    ASSERT_TRUE(DecodePayload(reply.data(), reply.size()));
    for (std::size_t n = 0; n < reply.size(); ++n)
        EXPECT_FALSE(DecodePayload(reply.data(), n));
    reply[16] = 4;
    EXPECT_FALSE(DecodePayload(reply.data(), reply.size()));
}
TEST(EthernetProtocol, ValidatesUdpTupleAndChecksums) {
    Route host;
    host.local_mac = {2, 1, 2, 3, 4, 5};
    host.remote_mac = {2, 6, 7, 8, 9, 10};
    const auto local = ParseIPv4("192.168.100.5");
    std::memcpy(&host.local_ip, local.data(), 4);
    const auto remote = ParseIPv4("192.168.100.10");
    std::memcpy(&host.remote_ip, remote.data(), 4);
    host.local_port = 40000;
    host.remote_port = 5000;
    Route board = host;
    std::swap(board.local_mac, board.remote_mac);
    std::swap(board.local_ip, board.remote_ip);
    std::swap(board.local_port, board.remote_port);
    MotorMessage message{};
    message.bus_idx = 4;
    message.data.id = 1;
    message.data.len = 2;
    auto payload = EncodePayload({message});
    payload[4] = 2;
    auto packet = EncodePacket(board, payload);
    ASSERT_TRUE(DecodePacket(host, packet.data(), packet.size()));
    EXPECT_FALSE(DecodePacket(board, packet.data(), packet.size()));
    packet.back() ^= 1;
    EXPECT_FALSE(DecodePacket(host, packet.data(), packet.size()));
    packet.back() ^= 1;
    for (std::size_t n = 0; n < packet.size(); ++n)
        EXPECT_FALSE(DecodePacket(host, packet.data(), n));
}
TEST(EthernetTransmitQueue, PreservesSynchronizedBatchBoundaries) {
    TransmitQueue queue;
    MotorMessages first;
    for (unsigned id = 0; id < 10; ++id) {
        MotorMessage message{};
        message.bus_idx = static_cast<int>(id % 8);
        message.data.id = id;
        first.push_back(message);
    }

    MotorMessage second_message{};
    second_message.bus_idx = 4;
    second_message.data.id = 100;
    const MotorMessages second(2, second_message);

    ASSERT_TRUE(queue.Push(first));
    ASSERT_TRUE(queue.Push(second));

    const auto first_output = queue.Pop();
    ASSERT_EQ(first_output.size(), first.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first_output[i].bus_idx, first[i].bus_idx);
        EXPECT_EQ(first_output[i].data.id, first[i].data.id);
    }
    const auto second_output = queue.Pop();
    ASSERT_EQ(second_output.size(), second.size());
    for (const auto& message : second_output) {
        EXPECT_EQ(message.bus_idx, second_message.bus_idx);
        EXPECT_EQ(message.data.id, second_message.data.id);
    }
    EXPECT_TRUE(queue.Empty());
}
TEST(EthernetTransmitQueue, RejectionIsAtomicAndQueuedBatchesRemainWhole) {
    TransmitQueue queue;
    MotorMessage m{};
    m.bus_idx = 4;
    for (int i = 0; i < 51; ++i)
        ASSERT_TRUE(queue.Push(MotorMessages(80, m)));
    ASSERT_TRUE(queue.Push(MotorMessages(15, m)));
    EXPECT_FALSE(queue.Push(MotorMessages(2, m)));
    m.bus_idx = 8;
    EXPECT_FALSE(queue.Push({m}));
    std::size_t count = 0;
    while (!queue.Empty()) {
        const auto batch = queue.Pop();
        EXPECT_TRUE(batch.size() == 80u || batch.size() == 15u);
        for (const auto& item : batch)
            EXPECT_EQ(item.bus_idx, 4);
        count += batch.size();
    }
    EXPECT_EQ(count, 4095u);
    EXPECT_TRUE(queue.Pop().empty());
}
}  // namespace encos::ethernet

#include "ethernet/ethernet_management.h"
#include "ethernet/protocol/gateway_wire.h"
namespace encos::ethernet {
TEST(EthernetManagement, RejectsMalformedForeignAndOutOfRangeEvents) {
    std::array<uint8_t, 16> id{};
    id[0] = 42;
    std::array<uint8_t, 64> packet{};
    gw_packet(packet.data(), GW_CAN_EVENT, 9, id.data());
    gw_put32(packet.data() + 32, 7);
    gw_put32(packet.data() + 44, 0x1234);
    auto event = DecodeCanError(packet.data(), packet.size(), id);
    ASSERT_TRUE(event);
    EXPECT_EQ(event->channel, 7U);
    EXPECT_EQ(event->ecr, 0x1234U);
    EXPECT_FALSE(DecodeCanError(packet.data(), 63, id));
    packet[4] = 2;
    EXPECT_FALSE(DecodeCanError(packet.data(), 64, id));
    packet[4] = 1;
    packet[12] ^= 1;
    EXPECT_FALSE(DecodeCanError(packet.data(), 64, id));
    packet[12] ^= 1;
    gw_put32(packet.data() + 32, 8);
    EXPECT_FALSE(DecodeCanError(packet.data(), 64, id));
    gw_put32(packet.data() + 32, 0);
    packet[5] = GW_INFO;
    EXPECT_FALSE(DecodeCanError(packet.data(), 64, id));
}
}  // namespace encos::ethernet

#include "ethernet/ethernet_slave.h"
TEST(EthernetSlaves, FixedSubnetAndPackedBusAddress) {
    using namespace encos::ethernet;
    EXPECT_EQ(SlaveId("192.168.100.1"), 0u);
    EXPECT_EQ(SlaveId("192.168.100.10"), 9u);
    EXPECT_EQ(SlaveId("192.168.100.253"), 252u);
    EXPECT_THROW(SlaveId("192.168.100.254"), std::invalid_argument);
    for (const auto* ip :
         {"192.168.101.10", "192.168.100.0", "192.168.100.255", "192.168.100.01", "192.168.100.1x"})
        EXPECT_THROW(SlaveId(ip), std::invalid_argument);
    EXPECT_EQ(BusSlave((10 << 16) | 7), 10u);
    EXPECT_EQ(BusSlave(0), 0u);
    EXPECT_EQ(BusSlave(7), 0u);
    EXPECT_EQ(BusSlave((252 << 16) | 7), 252u);
    for (int bus : {-1, 253 << 16, (10 << 16) | 8, (10 << 16) | 256, 255 << 16})
        EXPECT_THROW(BusSlave(bus), std::invalid_argument);
}

namespace encos::ethernet {
TEST(EthernetHeartbeat, DisconnectsAt500MillisecondsAndReportsTransitionOnce) {
    const ManagementHeartbeat::Clock::time_point start{};
    ManagementHeartbeat heartbeat(start);
    EXPECT_FALSE(heartbeat.CheckTimeout(start + std::chrono::milliseconds(499)));
    EXPECT_TRUE(heartbeat.CheckTimeout(start + std::chrono::milliseconds(500)));
    EXPECT_FALSE(heartbeat.Online());
    EXPECT_FALSE(heartbeat.CheckTimeout(start + std::chrono::seconds(2)));
    EXPECT_TRUE(heartbeat.Report(1, 2000, start + std::chrono::seconds(2)));
    EXPECT_TRUE(heartbeat.Online());
    EXPECT_FALSE(heartbeat.CheckTimeout(start + std::chrono::milliseconds(2499)));
    EXPECT_TRUE(heartbeat.CheckTimeout(start + std::chrono::milliseconds(2500)));
}
TEST(EthernetHeartbeat, DuplicateReportsCannotKeepOfflineGatewayAlive) {
    const ManagementHeartbeat::Clock::time_point start{};
    ManagementHeartbeat heartbeat(start);
    EXPECT_TRUE(heartbeat.Report(1, 100, start));
    EXPECT_TRUE(heartbeat.Report(2, 100, start));
    EXPECT_FALSE(heartbeat.Report(1, 100, start + std::chrono::milliseconds(400)));
    EXPECT_TRUE(heartbeat.CheckTimeout(start + std::chrono::milliseconds(500)));
    EXPECT_FALSE(heartbeat.Report(2, 100, start + std::chrono::milliseconds(600)));
    EXPECT_FALSE(heartbeat.Online());
    EXPECT_TRUE(heartbeat.Report(1, 10, start + std::chrono::milliseconds(700)));
    EXPECT_TRUE(heartbeat.Online());
}
TEST(EthernetHeartbeat, PeriodicIdleReportsTolerateOccasionalLoss) {
    const ManagementHeartbeat::Clock::time_point start{};
    ManagementHeartbeat heartbeat(start);
    for (uint32_t i = 1; i <= 100; ++i) {
        const auto now = start + std::chrono::milliseconds(i * 100);
        EXPECT_FALSE(heartbeat.CheckTimeout(now));
        if (i % 3 != 0)
            EXPECT_TRUE(heartbeat.Report(i, i * 100, now));
    }
    EXPECT_TRUE(heartbeat.Online());
}
}  // namespace encos::ethernet
