// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "utils/emr2/relay_frame.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <stdexcept>

namespace encos {
namespace {
MotorMessage Message(uint8_t length, uint32_t id = 1) {
    MotorMessage result{};
    result.bus_idx = 3;
    result.data.id = id;
    result.data.frame_flags = kCanFrameFlagFdMask;
    result.data.len = length;
    for (std::size_t i = 0; i < sizeof(result.data.data); ++i)
        result.data.data[i] = static_cast<uint8_t>(i + 1);
    return result;
}

TEST(Emr2Codec, LegacyBytesRemainIdentical) {
    MotorMessages messages{Message(0), Message(3), Message(8)};
    RelayWireOptions options;
    const auto packets = EncodeRelayPackets(RelayFrameType::RelayToHelper, messages, options);
    ASSERT_EQ(packets.size(), 1U);
    EXPECT_EQ(packets[0], EncodeRelayFrames(RelayFrameType::RelayToHelper, messages));
    const auto frames = DecodeRelayPackets(packets[0]);
    ASSERT_TRUE(frames.has_value());
    ASSERT_EQ(frames->size(), 1U);
    EXPECT_EQ(frames->front().protocol, RelayProtocol::Emr1);
    ASSERT_EQ(frames->front().records.size(), messages.size());
    for (std::size_t i = 0; i < messages.size(); ++i) {
        EXPECT_EQ(frames->front().records[i].message.data.len, messages[i].data.len);
        EXPECT_TRUE(std::equal(messages[i].data.data, messages[i].data.data + 8,
                               frames->front().records[i].message.data.data));
    }
}

TEST(Emr2Codec, DynamicLengthsAndIndependentBudgetedPackets) {
    MotorMessages messages;
    for (uint8_t len = 0; len <= 64; ++len)
        messages.push_back(Message(len, len + 1));
    RelayWireOptions options{RelayProtocol::Emr2, 128, 0x12345678, 100, 42};
    const auto packets = EncodeRelayPackets(RelayFrameType::HelperToRelay, messages, options);
    ASSERT_GT(packets.size(), 1U);
    std::size_t index = 0;
    for (std::size_t packet_index = 0; packet_index < packets.size(); ++packet_index) {
        const auto& bytes = packets[packet_index];
        EXPECT_LE(bytes.size(), 128U);
        auto decoded = DecodeRelayPackets(bytes);
        ASSERT_TRUE(decoded.has_value());
        ASSERT_EQ(decoded->size(), 1U);
        const auto& frame = decoded->front();
        EXPECT_EQ(frame.type, RelayFrameType::HelperToRelay);
        EXPECT_EQ(frame.session_id, 0x12345678U);
        EXPECT_EQ(frame.sequence, 100 + packet_index);
        EXPECT_EQ(frame.batch_id, 42U);
        std::size_t expected_size = 32;
        for (const auto& record : frame.records) {
            ASSERT_LT(index, messages.size());
            const auto& expected = messages[index++];
            EXPECT_EQ(record.message.bus_idx, expected.bus_idx);
            EXPECT_EQ(record.message.data.id, expected.data.id);
            EXPECT_EQ(record.message.data.len, expected.data.len);
            EXPECT_TRUE(std::equal(expected.data.data, expected.data.data + expected.data.len,
                                   record.message.data.data));
            for (std::size_t i = expected.data.len; i < 64; ++i)
                EXPECT_EQ(record.message.data.data[i], 0);
            expected_size += 12 + expected.data.len;
        }
        EXPECT_EQ(bytes.size(), expected_size);
    }
    EXPECT_EQ(index, messages.size());
    EXPECT_EQ(options.sequence, 100 + packets.size());
}

TEST(Emr2Codec, ExactBoundaryAndTooSmallBudget) {
    RelayWireOptions options{RelayProtocol::Emr2, 108, 1, 7, 2};
    const auto packets = EncodeRelayPackets(RelayFrameType::RelayToHelper,
                                            MotorMessages{Message(64), Message(64)}, options);
    ASSERT_EQ(packets.size(), 2U);
    EXPECT_EQ(packets[0].size(), 108U);
    options.max_payload = 107;
    EXPECT_THROW(EncodeRelayPackets(RelayFrameType::RelayToHelper,
                                    MotorMessages{Message(8), Message(64)}, options),
                 std::invalid_argument);
    EXPECT_EQ(options.sequence, 9U);
}

TEST(Emr2Codec, LegacyModeRejectsLongRecordAndRequiredMetadataBeforeChangingSequence) {
    RelayWireOptions options;
    options.sequence = 17;
    EXPECT_THROW(EncodeRelayPackets(RelayFrameType::RelayToHelper,
                                    MotorMessages{Message(8), Message(64)}, options),
                 std::invalid_argument);
    std::vector<RelayRecord> records{{Message(8), {{7, {1}}}}};
    EXPECT_THROW(EncodeRelayPackets(RelayFrameType::RelayToHelper, records, {}, options),
                 std::invalid_argument);
    options.protocol = RelayProtocol::Emr2;
    records[0].extensions[0].type = 0x8007;
    EXPECT_THROW(EncodeRelayPackets(RelayFrameType::RelayToHelper, records, {}, options),
                 std::invalid_argument);
    EXPECT_EQ(options.sequence, 17U);
}

TEST(Emr2Codec, PreservesPacketRecordTimestampAndUnknownOptionalExtensions) {
    RelayWireOptions options{RelayProtocol::Emr2, 160, 3, 4, 5};
    const std::vector<RelayExtension> metadata{{1, std::vector<uint8_t>(24, 0x32)}};
    std::vector<RelayRecord> records{{Message(64), {{23, {0x12, 0x34}}}}};
    const auto packets =
        EncodeRelayPackets(RelayFrameType::HelperToRelay, records, metadata, options);
    auto decoded = DecodeRelayPackets(packets[0]);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->front().extensions.size(), 1U);
    EXPECT_EQ(decoded->front().extensions[0].value, metadata[0].value);
    ASSERT_EQ(decoded->front().records[0].extensions.size(), 1U);
    EXPECT_EQ(decoded->front().records[0].extensions[0].type, 23);
    EXPECT_EQ(decoded->front().records[0].extensions[0].value, records[0].extensions[0].value);
    auto critical = packets[0];
    critical[33] |= 0x80;
    EXPECT_FALSE(DecodeRelayPackets(critical).has_value());
}

TEST(Emr2Codec, RejectsTruncationMalformedLengthsFlagsAndTrailingGarbageAtomically) {
    RelayWireOptions options{RelayProtocol::Emr2};
    const auto packets =
        EncodeRelayPackets(RelayFrameType::RelayToHelper, MotorMessages{Message(64)}, options);
    for (std::size_t size = 1; size < packets[0].size(); ++size) {
        EXPECT_FALSE(
            DecodeRelayPackets({packets[0].begin(), packets[0].begin() + size}).has_value());
    }
    for (const auto offset : {5, 6, 8, 10, 24, 28, 40, 41, 42}) {
        auto bad = packets[0];
        bad[offset] = 255;
        EXPECT_FALSE(DecodeRelayPackets(bad).has_value()) << offset;
        auto stream = packets[0];
        stream.insert(stream.end(), bad.begin(), bad.end());
        EXPECT_FALSE(DecodeRelayPackets(stream).has_value());
    }
    auto bad = packets[0];
    bad.push_back(0);
    EXPECT_FALSE(DecodeRelayPackets(bad).has_value());
}

TEST(Emr2Codec, DecodesMixedVersionsAndHeartbeat) {
    RelayWireOptions options;
    auto bytes =
        EncodeRelayPackets(RelayFrameType::HelperToRelay, MotorMessages{Message(8)}, options)[0];
    options.protocol = RelayProtocol::Emr2;
    const auto heartbeat =
        EncodeRelayPackets(RelayFrameType::HelperToRelay, MotorMessages{}, options)[0];
    EXPECT_EQ(heartbeat.size(), 32U);
    bytes.insert(bytes.end(), heartbeat.begin(), heartbeat.end());
    const auto decoded = DecodeRelayPackets(bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 2U);
    EXPECT_EQ((*decoded)[0].protocol, RelayProtocol::Emr1);
    EXPECT_EQ((*decoded)[1].protocol, RelayProtocol::Emr2);
    EXPECT_TRUE((*decoded)[1].records.empty());
}

TEST(Emr2Codec, CapabilityRoundTripAndConservativeSelection) {
    EXPECT_EQ(SelectRelayProtocol(RelayProtocolMode::Auto, {}), RelayProtocol::Emr1);
    EXPECT_EQ(SelectRelayProtocol(RelayProtocolMode::Emr1, {true, 1448, 0}), RelayProtocol::Emr1);
    EXPECT_EQ(SelectRelayProtocol(RelayProtocolMode::Auto, {true, 1448, 0}), RelayProtocol::Emr2);
    EXPECT_EQ(SelectRelayProtocol(RelayProtocolMode::Emr2, {}), RelayProtocol::Emr2);
    EXPECT_EQ(ParseRelayProtocolMode("auto"), RelayProtocolMode::Auto);
    EXPECT_THROW(ParseRelayProtocolMode("emr3"), std::invalid_argument);
    const RelayCapabilityMessage capability{true, 1234, {true, 1200, 0}};
    auto bytes = EncodeRelayCapability(capability);
    EXPECT_EQ(bytes.size(), 24U);
    auto decoded = DecodeRelayCapability(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->response);
    EXPECT_EQ(decoded->request_id, 1234U);
    EXPECT_TRUE(decoded->capabilities.emr2);
    EXPECT_EQ(decoded->capabilities.max_payload, 1200);
    EXPECT_FALSE(DecodeRelayPackets(bytes).has_value());
    bytes[18] = 1;
    EXPECT_FALSE(DecodeRelayCapability(bytes).has_value());
}
}  // namespace
}  // namespace encos
