// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <chrono>
#include <deque>
#include <gtest/gtest.h>
#include <thread>

#include "ethernet/ethernet_management.h"
#include "ethernet/protocol/gateway_wire.h"
#include "ethernet/transport/datagram.h"
namespace encos::ethernet {
namespace {
class DiscoveryGateway : public DiscoveryChannel {
public:
    std::deque<DiscoveryDatagram> packets;
    unsigned broadcasts = 0;
    bool duplicate = false;
    void Broadcast(const uint8_t* data, std::size_t size) override {
        EXPECT_EQ(size, 64U);
        EXPECT_EQ(data[5], GW_DISCOVER);
        ++broadcasts;
        // Exercise the retry path; return the same identity twice on the second broadcast.
        if (broadcasts == 1)
            return;
        std::vector<uint8_t> reply(64);
        gw_packet(reply.data(), GW_INFO, gw_get32(data + 8), nullptr);
        reply[12] = 42;
        reply[28] = 192;
        reply[29] = 168;
        reply[30] = 100;
        reply[31] = 11;
        reply[32] = reply[33] = reply[34] = 255;
        reply[47] = 8;
        const DiscoveryDatagram valid{"Ethernet 测试网卡", "192.168.100.11", 5001, reply};
        auto invalid = valid;
        invalid.port = 5000;
        packets.push_back(invalid);
        invalid = valid;
        invalid.address = "192.168.100.12";
        packets.push_back(invalid);
        invalid = valid;
        invalid.interface_name = "Other NIC";
        packets.push_back(invalid);
        invalid = valid;
        invalid.payload[32] = 254;
        packets.push_back(invalid);
        invalid = valid;
        invalid.payload[47] = 4;
        packets.push_back(invalid);
        invalid = valid;
        invalid.payload[12] = 0;
        packets.push_back(invalid);
        invalid = valid;
        invalid.payload.push_back(0);
        packets.push_back(invalid);
        invalid = valid;
        ++invalid.payload[8];
        packets.push_back(invalid);
        packets.push_back(valid);
        invalid = valid;
        if (duplicate)
            invalid.payload[12] = 43;
        packets.push_back(invalid);
    }
    DiscoveryDatagram Receive(int milliseconds) override {
        if (packets.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
            return {};
        }
        auto packet = std::move(packets.front());
        packets.pop_front();
        return packet;
    }
};
}  // namespace
TEST(EthernetDiscovery, RetriesAndRejectsForeignMalformedAndMismatchedResponses) {
    DiscoveryGateway gateway;
    const auto peers = DiscoverSlaves(gateway, "Ethernet 测试网卡");
    ASSERT_EQ(peers.size(), 1U);
    EXPECT_EQ(peers[0].address, "192.168.100.11");
    EXPECT_EQ(peers[0].interface_name, "Ethernet 测试网卡");
    EXPECT_EQ(gateway.broadcasts, 2U);
}
TEST(EthernetDiscovery, RejectsDuplicateSlaveIdWithDifferentIdentity) {
    DiscoveryGateway gateway;
    gateway.duplicate = true;
    EXPECT_THROW(DiscoverSlaves(gateway, "Ethernet 测试网卡"), std::runtime_error);
}
}  // namespace encos::ethernet
