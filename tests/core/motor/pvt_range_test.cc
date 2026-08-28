#include <atomic>
#include <gtest/gtest.h>
#include <thread>

#include "test_fixtures.h"

namespace encos {

class PVTRangeTests : public MotorTestFixture {};

#define EXPECT_RANGE_WRITE_TESTS(base_name, call_expr, policy_param, field_expr, expected_min, \
                                 expected_max, raw_min, raw_max)                               \
    TEST_F(PVTRangeTests, base_name##Failure) {                                                \
        adapter->SetParameterWritePolicy(0, 1, policy_param, FakeWritePolicy::Ignore);         \
        EXPECT_FALSE((call_expr));                                                             \
        const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);                \
        EXPECT_EQ(payload.parameter, policy_param);                                            \
        EXPECT_FALSE(payload.wait_for_ack);                                                    \
        ExpectRawBytes(                                                                        \
            payload.raw_value,                                                                 \
            {static_cast<uint8_t>((raw_min) >> 8), static_cast<uint8_t>((raw_min) & 0xFF),     \
             static_cast<uint8_t>((raw_max) >> 8), static_cast<uint8_t>((raw_max) & 0xFF)});   \
    }                                                                                          \
    TEST_F(PVTRangeTests, base_name##NoWait) {                                                 \
        EXPECT_TRUE((call_expr));                                                              \
        const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);                \
        EXPECT_EQ(payload.parameter, policy_param);                                            \
        EXPECT_FALSE(payload.wait_for_ack);                                                    \
        ExpectRawBytes(                                                                        \
            payload.raw_value,                                                                 \
            {static_cast<uint8_t>((raw_min) >> 8), static_cast<uint8_t>((raw_min) & 0xFF),     \
             static_cast<uint8_t>((raw_max) >> 8), static_cast<uint8_t>((raw_max) & 0xFF)});   \
    }                                                                                          \
    TEST_F(PVTRangeTests, base_name##Success) {                                                \
        EXPECT_TRUE((call_expr));                                                              \
        const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);                \
        EXPECT_EQ(payload.parameter, policy_param);                                            \
        EXPECT_FALSE(payload.wait_for_ack);                                                    \
        ExpectRawBytes(                                                                        \
            payload.raw_value,                                                                 \
            {static_cast<uint8_t>((raw_min) >> 8), static_cast<uint8_t>((raw_min) & 0xFF),     \
             static_cast<uint8_t>((raw_max) >> 8), static_cast<uint8_t>((raw_max) & 0xFF)});   \
        const auto snapshot = adapter->GetMotorSnapshot(0, 1);                                 \
        EXPECT_NEAR((field_expr).min, expected_min, kDecodedFloatTolerance);                   \
        EXPECT_NEAR((field_expr).max, expected_max, kDecodedFloatTolerance);                   \
    }                                                                                          \
    TEST_F(PVTRangeTests, base_name##Timeout) {                                                \
        adapter->SetReplyMode(FakeReplyMode::Manual);                                          \
        EXPECT_FALSE((call_expr));                                                             \
        const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);                \
        EXPECT_EQ(payload.parameter, policy_param);                                            \
        EXPECT_FALSE(payload.wait_for_ack);                                                    \
        ExpectRawBytes(                                                                        \
            payload.raw_value,                                                                 \
            {static_cast<uint8_t>((raw_min) >> 8), static_cast<uint8_t>((raw_min) & 0xFF),     \
             static_cast<uint8_t>((raw_max) >> 8), static_cast<uint8_t>((raw_max) & 0xFF)});   \
    }

TEST_F(PVTRangeTests, SetPVTCurRangeUpdatesSnapshotAndCommandRecord) {
    EXPECT_TRUE(motor->SetPVTCurRange({1.5f, 2.5f}, true));

    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.parameter, MotorParameter::PVTCurRange);
    EXPECT_FALSE(payload.wait_for_ack);
    ExpectRawBytes(payload.raw_value, {0x00, 0x0F, 0x00, 0x19});
    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot.ranges.current.min, 1.5f, kDecodedFloatTolerance);
    EXPECT_NEAR(snapshot.ranges.current.max, 2.5f, kDecodedFloatTolerance);
}

TEST_F(PVTRangeTests, SetPVTPosRangeCanBeIgnored) {
    adapter->SetParameterWritePolicy(0, 1, MotorParameter::PVTPosRange, FakeWritePolicy::Ignore);
    EXPECT_FALSE(motor->SetPVTPosRange({-1.23f, 4.56f}, true));

    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.parameter, MotorParameter::PVTPosRange);
    EXPECT_FALSE(payload.wait_for_ack);
    ExpectRawBytes(payload.raw_value, {0xFF, 0x85, 0x01, 0xC8});
    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NE(snapshot.ranges.position.max, 4.56f);
}

EXPECT_RANGE_WRITE_TESTS(SetPVTKdRange, motor->SetPVTKdRange({50, 150}, true),
                         MotorParameter::PVTKdRange, snapshot.ranges.kd, 50.0f, 150.0f, 50, 150)
EXPECT_RANGE_WRITE_TESTS(SetPVTKpRange, motor->SetPVTKpRange({100, 200}, true),
                         MotorParameter::PVTKpRange, snapshot.ranges.kp, 100.0f, 200.0f, 100, 200)
EXPECT_RANGE_WRITE_TESTS(SetPVTPosRange, motor->SetPVTPosRange({-1.23f, 4.56f}, true),
                         MotorParameter::PVTPosRange, snapshot.ranges.position, -1.23f, 4.56f,
                         static_cast<uint16_t>(static_cast<int16_t>(-123)),
                         static_cast<uint16_t>(static_cast<int16_t>(456)))
EXPECT_RANGE_WRITE_TESTS(SetPVTSpdRange, motor->SetPVTSpdRange({-12.34f, 56.78f}, true),
                         MotorParameter::PVTSpdRange, snapshot.ranges.speed, -12.34f, 56.78f,
                         static_cast<uint16_t>(static_cast<int16_t>(-1234)),
                         static_cast<uint16_t>(static_cast<int16_t>(5678)))
EXPECT_RANGE_WRITE_TESTS(SetPVTTorRange, motor->SetPVTTorRange({0.5f, 2.0f}, true),
                         MotorParameter::PVTTorRange, snapshot.ranges.torque, 0.5f, 2.0f, 5, 20)

TEST_F(PVTRangeTests, SetDriverPVTRanges) {
    MotorPVTRanges new_ranges = motor->GetPVTRanges();
    new_ranges.position = {10.0f, 20.0f};
    new_ranges.speed = {5.0f, 15.0f};
    new_ranges.current = {1.0f, 3.0f};
    new_ranges.torque = {0.5f, 2.0f};
    new_ranges.kp = {100.0f, 500.0f};
    new_ranges.kd = {10.0f, 50.0f};
    motor->SetDriverPVTRanges(new_ranges);
    const auto ranges = motor->GetPVTRanges();
    EXPECT_EQ(ranges.position.min, 10.0f);
    EXPECT_EQ(ranges.position.max, 20.0f);
}

TEST_F(PVTRangeTests, ConcurrentRangeReadsObserveCoherentSnapshots) {
    auto make_ranges = [](float value) {
        MotorPVTRanges ranges{};
        ranges.kp = {value, value};
        ranges.kd = {value, value};
        ranges.position = {value, value};
        ranges.speed = {value, value};
        ranges.torque = {value, value};
        ranges.current = {value, value};
        return ranges;
    };
    const auto first = make_ranges(1.0f);
    const auto second = make_ranges(2.0f);
    motor->SetDriverPVTRanges(first);

    std::atomic<bool> start{false};
    std::thread writer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (int iteration = 0; iteration < 10000; ++iteration) {
            motor->SetDriverPVTRanges((iteration & 1) == 0 ? second : first);
        }
    });
    start.store(true, std::memory_order_release);
    for (int iteration = 0; iteration < 10000; ++iteration) {
        const auto ranges = motor->GetPVTRanges();
        const float value = ranges.kp.min;
        EXPECT_TRUE(value == 1.0f || value == 2.0f);
        EXPECT_EQ(ranges.kp.max, value);
        EXPECT_EQ(ranges.kd.min, value);
        EXPECT_EQ(ranges.kd.max, value);
        EXPECT_EQ(ranges.position.min, value);
        EXPECT_EQ(ranges.position.max, value);
        EXPECT_EQ(ranges.speed.min, value);
        EXPECT_EQ(ranges.speed.max, value);
        EXPECT_EQ(ranges.torque.min, value);
        EXPECT_EQ(ranges.torque.max, value);
        EXPECT_EQ(ranges.current.min, value);
        EXPECT_EQ(ranges.current.max, value);
    }
    writer.join();
}

#undef EXPECT_RANGE_WRITE_TESTS

}  // namespace encos
