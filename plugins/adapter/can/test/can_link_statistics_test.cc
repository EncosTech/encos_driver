// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "can_link_statistics.h"

#include <cstring>
#include <gtest/gtest.h>
#include <linux/can/netlink.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <vector>

namespace encos::can {

namespace {

using Bytes = std::vector<std::uint8_t>;

void Attribute(Bytes& bytes, unsigned type, const void* data, std::size_t size) {
    const auto offset = bytes.size();
    bytes.resize(offset + RTA_SPACE(size), 0);
    const rtattr header{static_cast<unsigned short>(RTA_LENGTH(size)),
                        static_cast<unsigned short>(type)};
    std::memcpy(bytes.data() + offset, &header, sizeof(header));
    std::memcpy(bytes.data() + offset + RTA_LENGTH(0), data, size);
}

template <typename T>
void Attribute(Bytes& bytes, unsigned type, const T& value) {
    Attribute(bytes, type, &value, sizeof(value));
}

Bytes KernelLink(bool with_counters = true, bool truncated_counters = false) {
    Bytes bytes(NLMSG_SPACE(sizeof(ifinfomsg)), 0);
    ifinfomsg info{};
    info.ifi_index = 7;
    std::memcpy(bytes.data() + NLMSG_HDRLEN, &info, sizeof(info));

    rtnl_link_stats64 unrelated_totals{};
    unrelated_totals.rx_packets = 124;
    Attribute(bytes, IFLA_STATS64, unrelated_totals);

    Bytes can;
    const std::uint32_t unrelated_state = CAN_STATE_BUS_OFF;
    Attribute(can, IFLA_CAN_STATE, unrelated_state);
    const can_berr_counter counters{0, 12};
    if (with_counters)
        Attribute(can, IFLA_CAN_BERR_COUNTER, &counters, truncated_counters ? 1 : sizeof(counters));

    Bytes link_info;
    Attribute(link_info, IFLA_INFO_KIND, "can", 4);
    Attribute(link_info, IFLA_INFO_DATA, can.data(), can.size());
    Attribute(bytes, IFLA_LINKINFO, link_info.data(), link_info.size());

    nlmsghdr header{};
    header.nlmsg_len = static_cast<unsigned>(bytes.size());
    header.nlmsg_type = RTM_NEWLINK;
    std::memcpy(bytes.data(), &header, sizeof(header));
    return bytes;
}

TEST(SocketCanStatistics, DecodesCurrentErrorCountersAndRequiredSingleLevelAddress) {
    const auto bytes = KernelLink();
    const auto snapshot = DecodeLinkStatistics(bytes.data(), bytes.size(), 7);

    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->slave_id, 0);
    EXPECT_EQ(snapshot->bus_id, 0);

    ASSERT_TRUE(snapshot->error_counters);
    EXPECT_EQ(snapshot->error_counters->tx, 0U);
    EXPECT_EQ(snapshot->error_counters->rx, 12U);
    EXPECT_EQ(snapshot->error_counters->kind, ErrorCounterKind::Current);
}

TEST(SocketCanStatistics, MissingOrMalformedErrorCountersDoNotInventZeroSamples) {
    for (const auto& bytes : {KernelLink(false), KernelLink(true, true)}) {
        const auto snapshot = DecodeLinkStatistics(bytes.data(), bytes.size(), 7);
        ASSERT_TRUE(snapshot);
        EXPECT_FALSE(snapshot->error_counters);
    }
}

TEST(SocketCanStatistics, RejectsTruncatedAndForeignLinkMessages) {
    const auto bytes = KernelLink();

    EXPECT_FALSE(DecodeLinkStatistics(bytes.data(), bytes.size(), 8));
    EXPECT_FALSE(DecodeLinkStatistics(bytes.data(), bytes.size() - 1, 7));
    EXPECT_FALSE(DecodeLinkStatistics(bytes.data(), 0, 7));
}

}  // namespace

}  // namespace encos::can
