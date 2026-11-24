// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "gs_usb_protocol.h"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>

#include "gs_usb_message.h"

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

TEST(GsUsbProtocolTests, FdDlcUsesStandardPayloadLengthOnTheWire) {
    const std::array<uint8_t, 16> lengths{0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    for (uint8_t dlc = 0; dlc < lengths.size(); ++dlc) {
        SCOPED_TRACE(static_cast<unsigned>(dlc));
        Frame frame;
        frame.flags = kFrameFlagFd | kFrameFlagBrs;
        frame.can_id = 0x123;
        frame.dlc = dlc;
        for (std::size_t i = 0; i < sizeof(frame.data); ++i) {
            frame.data[i] = static_cast<uint8_t>(i + 1);
        }
        std::array<uint8_t, kFdFrameSize> encoded{};
        ASSERT_EQ(EncodeFrame(frame, encoded.data(), encoded.size()), kFdFrameSize);
        EXPECT_EQ(encoded[8], dlc);
        for (std::size_t i = 0; i < lengths[dlc]; ++i) {
            EXPECT_EQ(encoded[12 + i], frame.data[i]);
        }
        for (std::size_t i = lengths[dlc]; i < 64; ++i) {
            EXPECT_EQ(encoded[12 + i], 0);
        }
        // 独立构造接收负载，避免编码与解码的同类错误互相抵消。
        for (std::size_t i = 0; i < 64; ++i) {
            encoded[12 + i] = static_cast<uint8_t>(i + 1);
        }
        Frame decoded;
        ASSERT_EQ(DecodeFrame(encoded.data(), encoded.size(), &decoded), kFdFrameSize);
        EXPECT_EQ(decoded.dlc, dlc);
        for (std::size_t i = 0; i < lengths[dlc]; ++i) {
            EXPECT_EQ(decoded.data[i], frame.data[i]);
        }
        for (std::size_t i = lengths[dlc]; i < 64; ++i) {
            EXPECT_EQ(decoded.data[i], 0);
        }
    }
}

TEST(GsUsbProtocolTests, RejectsInvalidDlcAndFdRemoteFramesOnEncode) {
    std::array<uint8_t, kFdFrameSize> encoded{};
    Frame frame;
    frame.dlc = 9;
    EXPECT_EQ(EncodeFrame(frame, encoded.data(), encoded.size()), 0U);
    frame.flags = kFrameFlagFd;
    for (const uint8_t dlc : {16, 64, 255}) {
        frame.dlc = dlc;
        EXPECT_EQ(EncodeFrame(frame, encoded.data(), encoded.size()), 0U);
    }
    frame.dlc = 8;
    frame.can_id = kCanRtr | 1;
    EXPECT_EQ(EncodeFrame(frame, encoded.data(), encoded.size()), 0U);
}

TEST(GsUsbProtocolTests, InternalMessageRoundTripPadsEveryLengthThrough64Bytes) {
    const std::array<uint8_t, 16> lengths{0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    for (uint8_t length = 0; length <= 64; ++length) {
        SCOPED_TRACE(static_cast<unsigned>(length));
        uint8_t expected_dlc = 0;
        while (lengths[expected_dlc] < length) {
            ++expected_dlc;
        }
        EXPECT_EQ(LengthToDlc(length), expected_dlc);
        MotorPackMsg message{};
        message.id = 0x1ABCDE;
        message.frame_flags = kCanFrameFlagEff | kCanFrameFlagFdMask;
        message.len = length;
        // 内部尾部故意非零，线上填充不能泄漏 len 以外的数据。
        for (std::size_t i = 0; i < sizeof(message.data); ++i) {
            message.data[i] = static_cast<uint8_t>(i + 1);
        }
        const auto frame = EncodeMessage(message, 3, 7);
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->dlc, expected_dlc);
        EXPECT_EQ(frame->channel, 3);
        EXPECT_EQ(frame->echo_id, 7U);
        std::array<uint8_t, kFdFrameSize> wire{};
        ASSERT_EQ(EncodeFrame(*frame, wire.data(), wire.size()), kFdFrameSize);
        EXPECT_EQ(wire[8], expected_dlc);
        for (std::size_t i = length; i < 64; ++i) {
            EXPECT_EQ(wire[12 + i], 0);
        }
        Frame decoded;
        ASSERT_EQ(DecodeFrame(wire.data(), wire.size(), &decoded), kFdFrameSize);
        const auto received = DecodeMessage(decoded);
        ASSERT_TRUE(received.has_value());
        EXPECT_EQ(received->id, message.id);
        EXPECT_EQ(received->frame_flags, message.frame_flags);
        EXPECT_EQ(received->len, lengths[expected_dlc]);
        for (std::size_t i = 0; i < length; ++i) {
            EXPECT_EQ(received->data[i], message.data[i]);
        }
        for (std::size_t i = length; i < sizeof(received->data); ++i) {
            EXPECT_EQ(received->data[i], 0);
        }
    }
    EXPECT_FALSE(LengthToDlc(65).has_value());
    EXPECT_FALSE(LengthToDlc(255).has_value());
    EXPECT_FALSE(DlcToLength(16).has_value());
    EXPECT_FALSE(DlcToLength(255).has_value());
}

TEST(GsUsbProtocolTests, MessageBridgeRejectsInvalidFramesWithoutTruncation) {
    MotorPackMsg message{};
    message.len = 9;
    EXPECT_FALSE(EncodeMessage(message, 0, 1).has_value());
    message.frame_flags = kCanFrameFlagFdMask;
    message.len = 65;
    EXPECT_FALSE(EncodeMessage(message, 0, 1).has_value());
    message.len = 8;
    message.frame_flags = kCanFrameFlagFdBit1;
    EXPECT_FALSE(EncodeMessage(message, 0, 1).has_value());
    message.frame_flags = kCanFrameFlagFdMask | kCanFrameFlagRtr;
    EXPECT_FALSE(EncodeMessage(message, 0, 1).has_value());
    message.frame_flags = kCanFrameFlagFdMask;
    message.id = 0x800;
    EXPECT_FALSE(EncodeMessage(message, 0, 1).has_value());
    message.frame_flags |= kCanFrameFlagEff;
    message.id = 0x20000000;
    EXPECT_FALSE(EncodeMessage(message, 0, 1).has_value());
}

TEST(GsUsbProtocolTests, RejectsMalformedAndShortFdInput) {
    std::array<uint8_t, kFdFrameSize> wire{};
    wire[10] = kFrameFlagFd;
    wire[8] = 15;
    Frame decoded;
    EXPECT_EQ(DecodeFrame(wire.data(), wire.size() - 1, &decoded), 0U);
    for (const uint8_t dlc : {16, 64, 255}) {
        wire[8] = dlc;
        EXPECT_EQ(DecodeFrame(wire.data(), wire.size(), &decoded), 0U);
    }
    wire[8] = 15;
    WriteLe32(wire.data() + 4, kCanRtr | 1);
    EXPECT_EQ(DecodeFrame(wire.data(), wire.size(), &decoded), 0U);
    WriteLe32(wire.data() + 4, 1);
    wire[10] = kFrameFlagBrs;
    wire[8] = 8;
    EXPECT_EQ(DecodeFrame(wire.data(), wire.size(), &decoded), 0U);
    wire[10] = 0x80;
    EXPECT_EQ(DecodeFrame(wire.data(), wire.size(), &decoded), 0U);
}

TEST(GsUsbProtocolTests, ClassicRemoteFramePreservesLengthWithoutPayload) {
    MotorPackMsg message{};
    message.id = 0x123;
    message.len = 8;
    message.frame_flags = kCanFrameFlagRtr;
    for (auto& byte : message.data) {
        byte = 0xA5;
    }
    const auto frame = EncodeMessage(message, 1, 2);
    ASSERT_TRUE(frame.has_value());
    std::array<uint8_t, kClassicFrameSize> wire{};
    ASSERT_EQ(EncodeFrame(*frame, wire.data(), wire.size()), kClassicFrameSize);
    EXPECT_EQ(wire[8], 8);
    for (std::size_t i = 12; i < wire.size(); ++i) {
        EXPECT_EQ(wire[i], 0);
        wire[i] = 0xA5;
    }
    Frame decoded;
    ASSERT_EQ(DecodeFrame(wire.data(), wire.size(), &decoded), kClassicFrameSize);
    const auto received = DecodeMessage(decoded);
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->len, 8);
    EXPECT_EQ(received->frame_flags, kCanFrameFlagRtr);
    for (const auto byte : received->data) {
        EXPECT_EQ(byte, 0);
    }
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
