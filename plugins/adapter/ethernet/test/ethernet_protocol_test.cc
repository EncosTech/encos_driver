// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <cstring>
#include <gtest/gtest.h>

#include "ethernet/can_statistics.h"
#include "ethernet/ethernet_tx_queue.h"
#include "ethernet/management_heartbeat.h"
#include "ethernet/network_diagnostics.h"
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

namespace encos::ethernet {

TEST(EthernetStatistics, IdleHeartbeatRetainsLastErrorCountersAndValidZero) {
    BusStatistics bus;
    CanError event;
    ApplyCanStatistics(bus, event);
    EXPECT_FALSE(bus.error_counters);

    event.flags = 1;
    event.ecr = (37U << 8U) | 12U;
    ApplyCanStatistics(bus, event);
    ASSERT_TRUE(bus.error_counters);
    EXPECT_EQ(bus.error_counters->tx, 12U);
    EXPECT_EQ(bus.error_counters->rx, 37U);
    EXPECT_EQ(bus.error_counters->kind, ErrorCounterKind::LastErrorEvent);

    event.flags = 0;
    event.ecr = 0;
    ApplyCanStatistics(bus, event);
    EXPECT_EQ(bus.error_counters->tx, 12U);
    EXPECT_EQ(bus.error_counters->rx, 37U);

    event.flags = 1;
    ApplyCanStatistics(bus, event);
    EXPECT_EQ(bus.error_counters->tx, 0U);
    EXPECT_EQ(bus.error_counters->rx, 0U);
    EXPECT_EQ(bus.error_counters->kind, ErrorCounterKind::LastErrorEvent);
}

TEST(EthernetStatisticsOrder, RejectsOlderPacketsWithoutOverwritingLatestCounters) {
    CanStatisticsOrder ordering;
    const CanStatisticsOrder::Clock::time_point start{};
    CanError event;

    event.sequence = 20;
    event.uptime = 1000;

    event.flags = 1;
    event.ecr = 12;
    BusStatistics bus;
    ASSERT_EQ(ordering.Observe(event, start), CanStatisticsOrder::Result::Accepted);
    ApplyCanStatistics(bus, event);

    event.sequence = 19;
    event.uptime = 900;
    event.ecr = 33;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(100)),
              CanStatisticsOrder::Result::Rejected);
    ASSERT_TRUE(bus.error_counters);
    EXPECT_EQ(bus.error_counters->tx, 12U);

    event.sequence = 20;
    event.uptime = 1000;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(200)),
              CanStatisticsOrder::Result::Rejected);

    event.sequence = 21;
    event.uptime = 1100;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(200)),
              CanStatisticsOrder::Result::Accepted);
}

TEST(EthernetStatisticsOrder, AcceptsSerialSequenceAndUptimeWrap) {
    CanStatisticsOrder ordering;
    const CanStatisticsOrder::Clock::time_point start{};
    CanError event;

    event.sequence = 0xfffffff0U;
    event.uptime = 0xfffffff0U;
    EXPECT_EQ(ordering.Observe(event, start), CanStatisticsOrder::Result::Accepted);

    event.sequence = 3;
    event.uptime = 84;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(100)),
              CanStatisticsOrder::Result::Accepted);

    event.sequence = 0xfffffff8U;
    event.uptime = 0xfffffff8U;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(200)),
              CanStatisticsOrder::Result::Rejected);
}

TEST(EthernetStatisticsOrder, ConfirmsRebootAfterQuietIntervalAndRejectsPriorBootJump) {
    CanStatisticsOrder ordering;
    const CanStatisticsOrder::Clock::time_point start{};
    CanError event;

    event.sequence = 1000;
    event.uptime = 50000;
    ASSERT_EQ(ordering.Observe(event, start), CanStatisticsOrder::Result::Accepted);

    event.sequence = 8;
    event.uptime = 100;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(100)),
              CanStatisticsOrder::Result::Rejected);

    event.sequence = 16;
    event.uptime = 200;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(200)),
              CanStatisticsOrder::Result::Rejected);

    event.sequence = 24;
    event.uptime = 500;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(500)),
              CanStatisticsOrder::Result::Restarted);

    event.sequence = 999;
    event.uptime = 49900;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(600)),
              CanStatisticsOrder::Result::Rejected);

    event.sequence = 32;
    event.uptime = 600;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(600)),
              CanStatisticsOrder::Result::Accepted);
}

TEST(EthernetStatisticsOrder, IsolatedOldPacketsDoNotBecomeRebootsWhileCurrentReportsContinue) {
    CanStatisticsOrder ordering;
    const CanStatisticsOrder::Clock::time_point start{};
    CanError event;

    event.sequence = 1000;
    event.uptime = 50000;
    ASSERT_EQ(ordering.Observe(event, start), CanStatisticsOrder::Result::Accepted);

    event.sequence = 1;
    event.uptime = 1;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(100)),
              CanStatisticsOrder::Result::Rejected);

    event.sequence = 1008;
    event.uptime = 50100;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(100)),
              CanStatisticsOrder::Result::Accepted);

    event.sequence = 9;
    event.uptime = 101;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(600)),
              CanStatisticsOrder::Result::Rejected);
}

TEST(EthernetStatisticsOrder, RebootCanCoincideWithSequenceWrapAndLongOutageCanResume) {
    CanStatisticsOrder ordering;
    const CanStatisticsOrder::Clock::time_point start{};
    CanError event;

    event.sequence = 0xfffffff0U;
    event.uptime = 50000;
    ASSERT_EQ(ordering.Observe(event, start), CanStatisticsOrder::Result::Accepted);

    event.sequence = 1;
    event.uptime = 1;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(500)),
              CanStatisticsOrder::Result::Rejected);

    event.sequence = 9;
    event.uptime = 101;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(600)),
              CanStatisticsOrder::Result::Restarted);

    event.sequence = 100;
    event.uptime = 10101;
    EXPECT_EQ(ordering.Observe(event, start + std::chrono::milliseconds(10600)),
              CanStatisticsOrder::Result::Accepted);
}

}  // namespace encos::ethernet

#include "ethernet/transport/datagram.h"
#include "utils/emr2/relay_frame.h"

namespace encos::ethernet {
namespace {
class CapabilityChannel : public DatagramChannel {
public:
    bool available = true;
    bool wrong_identity = false;
    bool wrong_request = false;
    uint32_t budget = 512;
    uint32_t versions = 3;
    unsigned requests = 0;
    std::vector<uint8_t> reply;
    bool Send(const uint8_t* data, std::size_t size) override {
        ++requests;
        EXPECT_EQ(size, 64U);
        EXPECT_EQ(data[5], GW_CAPABILITIES_GET);
        if (available) {
            reply.assign(data, data + size);
            reply[5] = GW_CAPABILITIES;
            if (wrong_identity)
                reply[12] ^= 1;
            if (wrong_request)
                reply[8] ^= 1;
            gw_put32(reply.data() + 28, versions);
            gw_put32(reply.data() + 32, budget);
            gw_put32(reply.data() + 36, 7);
        }
        return true;
    }
    bool Wait(int) override {
        return !reply.empty();
    }
    std::size_t Receive(uint8_t* data, std::size_t) override {
        const auto size = reply.size();
        std::copy(reply.begin(), reply.end(), data);
        reply.clear();
        return size;
    }
};
}  // namespace
TEST(EthernetCapabilities, ConfirmsNewPeerAndBoundsLegacyFallback) {
    std::array<uint8_t, 16> identity{};
    identity[0] = 42;
    CapabilityChannel channel;
    auto caps = ProbeRelayCapabilities(channel, identity);
    EXPECT_TRUE(caps.emr2);
    EXPECT_EQ(caps.max_payload, 512U);
    EXPECT_EQ(caps.features, 7U);
    EXPECT_EQ(SelectRelayProtocol(RelayProtocolMode::Auto, caps), RelayProtocol::Emr2);
    channel.available = false;
    const auto start = std::chrono::steady_clock::now();
    caps = ProbeRelayCapabilities(channel, identity);
    EXPECT_FALSE(caps.emr2);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(1));
    EXPECT_EQ(SelectRelayProtocol(RelayProtocolMode::Auto, caps), RelayProtocol::Emr1);
}
TEST(EthernetCapabilities, RechecksDelayedHeartbeatWithoutDowngradingConfirmedPeer) {
    std::array<uint8_t, 16> identity{};
    identity[0] = 42;
    CapabilityChannel channel;
    const auto confirmed = ProbeRelayCapabilities(channel, identity);
    ASSERT_TRUE(confirmed.emr2);
    EXPECT_TRUE(RecheckRelayCapabilities(channel, identity, confirmed, 2000, 1000));
    EXPECT_EQ(channel.requests, 2U);
    EXPECT_TRUE(RecheckRelayCapabilities(channel, identity, confirmed, 2000, 2100));
    EXPECT_TRUE(RecheckRelayCapabilities(channel, identity, confirmed, 0xfffffff0U, 32));
    EXPECT_EQ(channel.requests, 2U);
    channel.versions = 1;
    EXPECT_FALSE(RecheckRelayCapabilities(channel, identity, confirmed, 2000, 1));
    channel.versions = 3;
    channel.budget = 256;
    EXPECT_FALSE(RecheckRelayCapabilities(channel, identity, confirmed, 2000, 1));
}
TEST(EthernetCapabilities, RejectsForeignStaleAndOversizedCapabilities) {
    std::array<uint8_t, 16> identity{};
    CapabilityChannel channel;
    channel.wrong_identity = true;
    EXPECT_FALSE(ProbeRelayCapabilities(channel, identity).emr2);
    channel.wrong_identity = false;
    channel.wrong_request = true;
    EXPECT_FALSE(ProbeRelayCapabilities(channel, identity).emr2);
    channel.wrong_request = false;
    channel.budget = 1473;
    EXPECT_FALSE(ProbeRelayCapabilities(channel, identity).emr2);
}
TEST(EthernetProtocol, Emr2BudgetKeepsComplete64ByteRecordsInSeparateDatagrams) {
    MotorMessage message{};
    message.bus_idx = 3;
    message.data.id = 0x123;
    message.data.frame_flags = kCanFrameFlagFdMask;
    message.data.len = 64;
    for (unsigned i = 0; i < 64; ++i)
        message.data.data[i] = static_cast<uint8_t>(i);
    RelayWireOptions options;
    options.protocol = RelayProtocol::Emr2;
    options.max_payload = 256;
    const auto packets =
        EncodeRelayPackets(RelayFrameType::HelperToRelay, MotorMessages(20, message), options);
    ASSERT_GT(packets.size(), 1U);
    std::size_t count = 0;
    for (const auto& packet : packets) {
        EXPECT_LE(packet.size(), 256U);
        const auto decoded = DecodePayload(packet.data(), packet.size());
        ASSERT_TRUE(decoded);
        for (const auto& result : *decoded) {
            EXPECT_EQ(result.data.len, 64);
            EXPECT_EQ(result.data.data[63], 63);
        }
        count += decoded->size();
    }
    EXPECT_EQ(count, 20U);
    auto joined = packets[0];
    joined.insert(joined.end(), packets[1].begin(), packets[1].end());
    EXPECT_FALSE(DecodePayload(joined.data(), joined.size()));
}
}  // namespace encos::ethernet

namespace encos::ethernet {
TEST(EthernetProtocol, PreservesLegacyConcatenationAnd80RecordLimit) {
    MotorMessage message{};
    message.data.id = 1;
    auto first = EncodePayload(MotorMessages(30, message));
    auto second = EncodePayload(MotorMessages(40, message));
    first[4] = 2;
    second[4] = 2;
    first.insert(first.end(), second.begin(), second.end());
    const auto decoded = DecodePayload(first.data(), first.size());
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->size(), 70U);
    const auto oversized =
        EncodeRelayFrames(RelayFrameType::HelperToRelay, MotorMessages(81, message));
    EXPECT_FALSE(DecodePayload(oversized.data(), oversized.size()));
}
}  // namespace encos::ethernet

TEST(EthernetCapabilities, UsesOpcodesOutsideLegacyIpAndCommandRange) {
    using namespace encos::ethernet;
    CapabilityChannel channel;
    std::array<uint8_t, 16> identity{};
    EXPECT_EQ(GW_CAPABILITIES_GET, 14U);
    EXPECT_EQ(GW_CAPABILITIES, 15U);
    EXPECT_TRUE(ProbeRelayCapabilities(channel, identity).emr2);
}

namespace encos::ethernet {

TEST(EthernetDiagnostics, ValidatesWireIdentityScopeAndReservedFields) {
    std::array<uint8_t, 16> id{};
    id[0] = 9;
    std::array<uint8_t, 64> p{};
    gw_packet(p.data(), GW_NETWORK_EVENT, 1, id.data());
    gw_put32(p.data() + 40, 42);
    auto e = DecodeNetworkDiagnostic(p.data(), p.size(), id);
    ASSERT_TRUE(e);
    EXPECT_EQ(e->values[0], 42U);
    EXPECT_FALSE(DecodeNetworkDiagnostic(p.data(), 63, id));
    p[12] ^= 1;
    EXPECT_FALSE(DecodeNetworkDiagnostic(p.data(), 64, id));
    p[12] ^= 1;
    gw_put32(p.data() + 36, 3);
    EXPECT_FALSE(DecodeNetworkDiagnostic(p.data(), 64, id));
    gw_put32(p.data() + 36, 0);
    gw_put32(p.data() + 32, 2);
    gw_put32(p.data() + 60, 1);
    EXPECT_FALSE(DecodeNetworkDiagnostic(p.data(), 64, id));
}

TEST(EthernetDiagnostics, ReportsBaselineDeltaWrapAndSuppressesDuplicateAndReordered) {
    NetworkDiagnosticTracker tracker;
    NetworkDiagnostic e;
    e.sequence = 1;
    e.values[0] = 0xfffffffeU;
    auto text = tracker.Update(e);
    ASSERT_TRUE(text);
    EXPECT_NE(text->find("baseline(since boot)"), std::string::npos);
    EXPECT_FALSE(tracker.Update(e));
    e.sequence = 2;
    EXPECT_FALSE(tracker.Update(e));
    e.sequence = 3;
    e.values[0] = 1;
    text = tracker.Update(e);
    ASSERT_TRUE(text);
    EXPECT_NE(text->find("rx_fifo_overflow=1(+3)"), std::string::npos);
    e.sequence = 2;
    EXPECT_FALSE(tracker.Update(e));
    tracker.Reset();
    e.sequence = 1;
    EXPECT_TRUE(tracker.Update(e));
}

TEST(EthernetDiagnostics, QuietHealthyDmaAndReportsFaultAndQueues) {
    NetworkDiagnosticTracker tracker;
    NetworkDiagnostic e;
    e.group = 1;
    e.sequence = 1;
    e.values[4] = 0x58;
    EXPECT_FALSE(tracker.Update(e));
    e.sequence++;
    e.values[0] = 1;
    e.values[2] = 1;
    auto text = tracker.Update(e);
    ASSERT_TRUE(text);
    EXPECT_NE(text->find("timeouts=1(+1)"), std::string::npos);
    EXPECT_FALSE(tracker.Update(e));
    e = {};
    e.sequence = 1;
    e.group = 3;
    e.index = 7;
    e.values[2] = 12;
    text = tracker.Update(e);
    ASSERT_TRUE(text);
    EXPECT_NE(text->find("CAN queue=7"), std::string::npos);
}

TEST(EthernetDiagnostics, PrintsSignedLwipError) {
    NetworkDiagnosticTracker tracker;
    NetworkDiagnostic e;
    e.sequence = 1;
    e.group = 2;
    e.values[2] = 1;
    e.values[3] = static_cast<uint32_t>(-11);
    const auto text = tracker.Update(e);
    ASSERT_TRUE(text);
    EXPECT_NE(text->find("last_send_error=-11"), std::string::npos);
}

}  // namespace encos::ethernet
