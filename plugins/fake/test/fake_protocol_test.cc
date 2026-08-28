#include "plugins/fake/fake_protocol.h"

#include <cmath>
#include <gtest/gtest.h>

#include "motor/types.h"

namespace encos {

namespace {

constexpr double kPi = 3.14159265358979323846;

MotorMessage MakeSetIdMessage(int bus_idx, int source_id, int target_id) {
    MotorPackMsg pack{};
    pack.id = 0x7FF;
    pack.len = 6;
    pack.data[0] = static_cast<uint8_t>(source_id >> 8);
    pack.data[1] = static_cast<uint8_t>(source_id & 0xFF);
    pack.data[2] = 0x00;
    pack.data[3] = 0x04;
    pack.data[4] = static_cast<uint8_t>(target_id >> 8);
    pack.data[5] = static_cast<uint8_t>(target_id & 0xFF);
    return MotorMessage{bus_idx, pack};
}

MotorMessage MakeSetPosMessage(int bus_idx, int source_id, float position_rad) {
    const int16_t centideg =
        static_cast<int16_t>(position_rad * 100.0f * 180.0f / static_cast<float>(kPi));
    MotorPackMsg pack{};
    pack.id = 0x7FF;
    pack.len = 6;
    pack.data[0] = static_cast<uint8_t>(source_id >> 8);
    pack.data[1] = static_cast<uint8_t>(source_id & 0xFF);
    pack.data[2] = 0x00;
    pack.data[3] = 0x03;
    pack.data[4] = static_cast<uint8_t>(centideg >> 8);
    pack.data[5] = static_cast<uint8_t>(centideg & 0xFF);
    return MotorMessage{bus_idx, pack};
}

MotorMessage MakeResetZeroPosMessage(int bus_idx, int source_id) {
    MotorPackMsg pack{};
    pack.id = 0x7FF;
    pack.len = 4;
    pack.data[0] = static_cast<uint8_t>(source_id >> 8);
    pack.data[1] = static_cast<uint8_t>(source_id & 0xFF);
    pack.data[2] = 0x00;
    pack.data[3] = 0x03;
    return MotorMessage{bus_idx, pack};
}

}  // namespace

TEST(FakeProtocolTests, SetIdRecordUsesSourceMotorId) {
    const auto record = DecodeFakeCommand(MakeSetIdMessage(2, 5, 9), nullptr);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->bus_idx, 2);
    EXPECT_EQ(record->motor_idx, 5);

    const auto* payload = std::get_if<FakeSetIdPayload>(&record->payload);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->source_id, 5);
    EXPECT_EQ(payload->target_id, 9);
}

TEST(FakeProtocolTests, SetPosRecordUsesSourceMotorId) {
    const auto record = DecodeFakeCommand(MakeSetPosMessage(0, 3, 0.5f), nullptr);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->bus_idx, 0);
    EXPECT_EQ(record->motor_idx, 3);

    const auto* payload = std::get_if<FakeSetPosPayload>(&record->payload);
    ASSERT_NE(payload, nullptr);
    EXPECT_NEAR(payload->position_rad, 0.5f, 0.01f);
}

TEST(FakeProtocolTests, ResetZeroPosRecordUsesSourceMotorId) {
    const auto record = DecodeFakeCommand(MakeResetZeroPosMessage(1, 7), nullptr);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->bus_idx, 1);
    EXPECT_EQ(record->motor_idx, 7);
    EXPECT_EQ(record->kind, FakeCommandKind::ResetZeroPos);
}

}  // namespace encos
