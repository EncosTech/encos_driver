// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "gs_usb_protocol.h"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>

namespace encos::gs_usb {
namespace {

std::array<std::uint8_t, 72> FirmwareBtConstExt() {
    const std::uint32_t values[18] = {
        kFeatureListenOnly | kFeatureLoopback | kFeatureFd | kFeatureBtConstExt | kFeatureGetState,
        80000000,
        1,
        255,
        2,
        128,
        128,
        1,
        512,
        1,
        1,
        31,
        2,
        16,
        16,
        1,
        32,
        1};
    std::array<std::uint8_t, 72> bytes{};
    for (std::size_t i = 0; i < 18U; ++i) {
        WriteLe32(bytes.data() + i * 4U, values[i]);
    }
    return bytes;
}

TEST(GsUsbProtocolTests, LittleEndianRoundTrip) {
    std::uint8_t buffer[4] = {};
    WriteLe32(buffer, 0x12345678U);
    EXPECT_EQ(buffer[0], 0x78U);
    EXPECT_EQ(buffer[3], 0x12U);
    EXPECT_EQ(ReadLe32(buffer), 0x12345678U);
}

TEST(GsUsbProtocolTests, DecodesBtConstExt) {
    const auto bytes = FirmwareBtConstExt();
    BtConstExt parsed;
    ASSERT_TRUE(DecodeBtConstExt(bytes.data(), bytes.size(), &parsed));
    EXPECT_EQ(parsed.fclk, 80000000U);
    EXPECT_EQ(parsed.tseg1_min, 1U);
    EXPECT_EQ(parsed.tseg1_max, 255U);
    EXPECT_EQ(parsed.tseg2_max, 128U);
    EXPECT_EQ(parsed.brp_max, 512U);
    EXPECT_EQ(parsed.dtseg1_max, 31U);
    EXPECT_EQ(parsed.dbrp_max, 32U);
    EXPECT_NE(parsed.feature & kFeatureFd, 0U);
}

TEST(GsUsbProtocolTests, ComputesNominalAndDataTiming) {
    const auto bytes = FirmwareBtConstExt();
    BtConstExt parsed;
    ASSERT_TRUE(DecodeBtConstExt(bytes.data(), bytes.size(), &parsed));

    const auto nominal = ComputeBitTiming(parsed, 1000000U, 0.765, false);
    ASSERT_TRUE(nominal.has_value());
    EXPECT_EQ(nominal->brp, 1U);
    EXPECT_EQ(nominal->phase_seg1, 60U);
    EXPECT_EQ(nominal->phase_seg2, 19U);
    const double nominal_sample =
        static_cast<double>(1U + nominal->phase_seg1) /
        static_cast<double>(1U + nominal->phase_seg1 + nominal->phase_seg2);
    EXPECT_NEAR(nominal_sample, 0.765, 0.02);

    const auto data = ComputeBitTiming(parsed, 5000000U, 0.882, true);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data->brp, 1U);
    EXPECT_EQ(data->phase_seg1, 13U);
    EXPECT_EQ(data->phase_seg2, 2U);
    EXPECT_LE(data->sjw, parsed.dsjw_max);

    std::uint8_t encoded[20] = {};
    ASSERT_TRUE(EncodeBitTiming(*nominal, encoded));
    EXPECT_EQ(ReadLe32(encoded + 4), 60U);
    EXPECT_EQ(ReadLe32(encoded + 8), 19U);
    EXPECT_EQ(ReadLe32(encoded + 16), 1U);
}

TEST(GsUsbProtocolTests, RejectsImpossibleTiming) {
    const auto bytes = FirmwareBtConstExt();
    BtConstExt parsed;
    ASSERT_TRUE(DecodeBtConstExt(bytes.data(), bytes.size(), &parsed));
    EXPECT_FALSE(ComputeBitTiming(parsed, 7000000U, 0.8, false).has_value());
}

TEST(GsUsbProtocolTests, ClassicFrameRoundTrip) {
    Frame frame;
    frame.echo_id = 7U;
    frame.can_id = 0x123U;
    frame.dlc = 2U;
    frame.channel = 3U;
    frame.data[0] = 0xABU;
    frame.data[1] = 0xCDU;

    std::uint8_t encoded[kFdFrameSize] = {};
    const std::size_t size = EncodeFrame(frame, encoded, sizeof(encoded));
    ASSERT_EQ(size, kClassicFrameSize);

    Frame decoded;
    ASSERT_EQ(DecodeFrame(encoded, size, &decoded), size);
    EXPECT_EQ(decoded.echo_id, 7U);
    EXPECT_EQ(decoded.can_id, 0x123U);
    EXPECT_EQ(decoded.dlc, 2U);
    EXPECT_EQ(decoded.channel, 3U);
    EXPECT_EQ(decoded.data[0], 0xABU);
    EXPECT_EQ(decoded.data[1], 0xCDU);
}

TEST(GsUsbProtocolTests, FdFrameRoundTrip) {
    Frame frame;
    frame.echo_id = kEchoBusFrame;
    frame.can_id = 0x1ABCDEFU | kCanEff;
    frame.dlc = 8U;
    frame.channel = 1U;
    frame.flags = kFrameFlagFd | kFrameFlagBrs;
    for (std::size_t i = 0; i < 8U; ++i) {
        frame.data[i] = static_cast<std::uint8_t>(i);
    }

    std::uint8_t encoded[kFdFrameSize] = {};
    const std::size_t size = EncodeFrame(frame, encoded, sizeof(encoded));
    ASSERT_EQ(size, kFdFrameSize);

    Frame decoded;
    ASSERT_EQ(DecodeFrame(encoded, size, &decoded), size);
    EXPECT_EQ(decoded.echo_id, kEchoBusFrame);
    EXPECT_EQ(decoded.can_id, 0x1ABCDEFU | kCanEff);
    EXPECT_EQ(decoded.flags, kFrameFlagFd | kFrameFlagBrs);
    EXPECT_EQ(decoded.channel, 1U);
}

}  // namespace
}  // namespace encos::gs_usb
