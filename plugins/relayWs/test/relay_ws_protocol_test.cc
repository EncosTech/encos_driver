#include <gtest/gtest.h>

#include "relay/relay_frame.h"

namespace encos {

class RelayFrameTest : public ::testing::Test {
protected:
    MotorMessage MakeMessage(int bus_idx, uint32_t id, uint8_t frame_flags, uint8_t len,
                             const std::vector<uint8_t>& data) {
        MotorMessage msg{};
        msg.bus_idx = bus_idx;
        msg.data.id = id;
        msg.data.frame_flags = frame_flags;
        msg.data.len = len;
        for (std::size_t i = 0; i < data.size() && i < 8; ++i) {
            msg.data.data[i] = data[i];
        }
        return msg;
    }
};

TEST_F(RelayFrameTest, EncodesEmptyFrame) {
    const auto bytes = EncodeRelayFrames(RelayFrameType::RelayToHelper, {});
    ASSERT_EQ(bytes.size(), 8u);
    EXPECT_EQ(bytes[0], 'E');
    EXPECT_EQ(bytes[1], 'M');
    EXPECT_EQ(bytes[2], 'R');
    EXPECT_EQ(bytes[3], '1');
    EXPECT_EQ(bytes[4], 1u);
    EXPECT_EQ(bytes[5], 0u);
    EXPECT_EQ(bytes[6], 0u);
    EXPECT_EQ(bytes[7], 0u);

    const auto decoded = DecodeRelayFrames(bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    EXPECT_EQ((*decoded)[0].type, RelayFrameType::RelayToHelper);
    EXPECT_TRUE((*decoded)[0].records.empty());
}

TEST_F(RelayFrameTest, EncodesAndDecodesSingleRecord) {
    auto msg = MakeMessage(3, 0x123, 0x01, 4, {0xAA, 0xBB, 0xCC, 0xDD});
    const auto bytes = EncodeRelayFrames(RelayFrameType::HelperToRelay, {msg});
    ASSERT_EQ(bytes.size(), 8u + 18u);

    const auto decoded = DecodeRelayFrames(bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    EXPECT_EQ((*decoded)[0].type, RelayFrameType::HelperToRelay);
    ASSERT_EQ((*decoded)[0].records.size(), 1u);
    EXPECT_EQ((*decoded)[0].records[0].bus_idx, 3);
    EXPECT_EQ((*decoded)[0].records[0].data.id, 0x123u);
    EXPECT_EQ((*decoded)[0].records[0].data.frame_flags, 0x01u);
    EXPECT_EQ((*decoded)[0].records[0].data.len, 4u);
    EXPECT_EQ((*decoded)[0].records[0].data.data[0], 0xAAu);
    EXPECT_EQ((*decoded)[0].records[0].data.data[1], 0xBBu);
    EXPECT_EQ((*decoded)[0].records[0].data.data[2], 0xCCu);
    EXPECT_EQ((*decoded)[0].records[0].data.data[3], 0xDDu);
}

TEST_F(RelayFrameTest, EncodesAndDecodesMultipleRecords) {
    std::vector<MotorMessage> records;
    for (int i = 0; i < 5; ++i) {
        records.push_back(MakeMessage(i, static_cast<uint32_t>(i), static_cast<uint8_t>(i), 8,
                                      {1, 2, 3, 4, 5, 6, 7, 8}));
    }
    const auto bytes = EncodeRelayFrames(RelayFrameType::RelayToHelper, records);
    ASSERT_EQ(bytes.size(), 8u + 5u * 18u);

    const auto decoded = DecodeRelayFrames(bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    ASSERT_EQ((*decoded)[0].records.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ((*decoded)[0].records[i].bus_idx, i);
        EXPECT_EQ((*decoded)[0].records[i].data.id, static_cast<uint32_t>(i));
    }
}

TEST_F(RelayFrameTest, SplitsRecordsAtCount255) {
    std::vector<MotorMessage> records;
    for (int i = 0; i < 260; ++i) {
        records.push_back(MakeMessage(0, static_cast<uint32_t>(i), 0, 8, {1, 2, 3, 4, 5, 6, 7, 8}));
    }
    const auto bytes = EncodeRelayFrames(RelayFrameType::HelperToRelay, records);
    ASSERT_EQ(bytes.size(), 2u * 8u + 260u * 18u);

    const auto decoded = DecodeRelayFrames(bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 2u);
    EXPECT_EQ((*decoded)[0].records.size(), 255u);
    EXPECT_EQ((*decoded)[1].records.size(), 5u);
}

TEST_F(RelayFrameTest, RejectsWrongMagic) {
    auto bytes = EncodeRelayFrames(RelayFrameType::RelayToHelper, {});
    bytes[0] = 'X';
    EXPECT_FALSE(DecodeRelayFrames(bytes).has_value());
}

TEST_F(RelayFrameTest, RejectsUnsupportedType) {
    auto bytes = EncodeRelayFrames(RelayFrameType::RelayToHelper, {});
    bytes[4] = 3;
    EXPECT_FALSE(DecodeRelayFrames(bytes).has_value());
}

TEST_F(RelayFrameTest, RejectsInvalidByteLength) {
    auto bytes = EncodeRelayFrames(RelayFrameType::RelayToHelper,
                                   {MakeMessage(0, 0, 0, 8, {1, 2, 3, 4, 5, 6, 7, 8})});
    bytes.resize(bytes.size() - 1);
    EXPECT_FALSE(DecodeRelayFrames(bytes).has_value());
}

TEST_F(RelayFrameTest, RejectsLenGreaterThan8) {
    auto msg = MakeMessage(0, 0, 0, 8, {1, 2, 3, 4, 5, 6, 7, 8});
    auto bytes = EncodeRelayFrames(RelayFrameType::RelayToHelper, {msg});
    bytes[17] = 9;  // len field in first record
    EXPECT_FALSE(DecodeRelayFrames(bytes).has_value());
}

}  // namespace encos
