#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <thread>
#include <vector>

#include "managed_adapter_test.h"
#include "motor/pack_helper.h"
#include "test_fixtures.h"

namespace encos {

class SpdControlTests : public MotorTestFixture {};

namespace {
MotorStatus MakeStatus(MotorError error = MotorError::NoError) {
    MotorStatus status{};
    status.error = error;
    status.position = 1.0f;
    status.speed = 2.0f;
    status.current = 3.0f;
    status.motor_temperature = 25.0f;
    status.mos_temperature = 30.0f;
    return status;
}
}  // namespace

TEST_F(SpdControlTests, SpdControlNoResp1) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    auto res = motor->SpdControl<1>(1.0f, 5.0f);
    EXPECT_EQ(res.error, MotorError::NoResponse);
    const auto& payload = LastPayloadAs<FakeSpdControlPayload>(*adapter);
    EXPECT_NEAR(payload.speed, 1.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.current, 5.0f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 1);
}

TEST_F(SpdControlTests, SpdControlNoResp2) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    auto res = motor->SpdControl<2>(1.0f, 5.0f);
    EXPECT_EQ(res.error, MotorError::NoResponse);
    EXPECT_EQ(LastPayloadAs<FakeSpdControlPayload>(*adapter).feedback_type, 2);
}

TEST_F(SpdControlTests, SpdControlNoResp3) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    auto res = motor->SpdControl<3>(1.0f, 5.0f);
    EXPECT_EQ(res.error, MotorError::NoResponse);
    EXPECT_EQ(LastPayloadAs<FakeSpdControlPayload>(*adapter).feedback_type, 3);
}

TEST_F(SpdControlTests, SpdControlDecodesFormattedCommandAndUpdatesSnapshot) {
    const auto result = motor->SpdControl<3>(1.0f, 5.0f);

    const auto& payload = LastPayloadAs<FakeSpdControlPayload>(*adapter);
    EXPECT_NEAR(payload.speed, 1.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.current, 5.0f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 3);

    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot.current_a, payload.current, kDecodedFloatTolerance);
    EXPECT_NEAR(snapshot.speed_rad_s, payload.speed, kDecodedAngleTolerance);
    EXPECT_EQ(result.error, MotorError::NoError);
}

TEST_F(SpdControlTests, SpdControlOverCurrent) {
    auto snapshot = adapter->GetMotorSnapshot(0, 1);
    snapshot.error = MotorError::OverCurrent;
    adapter->SeedMotor(0, 1, snapshot);

    const auto result = motor->SpdControl<2>(1.0f, 5.0f);
    EXPECT_EQ(result.error, MotorError::OverCurrent);
}

TEST_F(SpdControlTests, SpdControlTest0) {
    motor->SpdControl<0>(1.0f, 5.0f);
    const auto& payload = LastPayloadAs<FakeSpdControlPayload>(*adapter);
    EXPECT_NEAR(payload.speed, 1.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.current, 5.0f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 0);
}

TEST_F(SpdControlTests, SpdControlTest1) {
    const auto result = motor->SpdControl<1>(1.0f, 5.0f);
    const auto& payload = LastPayloadAs<FakeSpdControlPayload>(*adapter);
    EXPECT_NEAR(payload.speed, 1.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.current, 5.0f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 1);
    EXPECT_EQ(result.error, MotorError::NoError);
}

TEST_F(SpdControlTests, SpdControlTest2) {
    const auto result = motor->SpdControl<2>(1.0f, 5.0f);
    const auto& payload = LastPayloadAs<FakeSpdControlPayload>(*adapter);
    EXPECT_NEAR(payload.speed, 1.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.current, 5.0f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 2);
    EXPECT_EQ(result.error, MotorError::NoError);
}

TEST_F(SpdControlTests, SpdControlTest3) {
    const auto result = motor->SpdControl<3>(1.0f, 5.0f);
    const auto& payload = LastPayloadAs<FakeSpdControlPayload>(*adapter);
    EXPECT_NEAR(payload.speed, 1.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.current, 5.0f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 3);
    EXPECT_EQ(result.error, MotorError::NoError);
}

TEST(MotorErrorTests, ValuesMatchFeedbackProtocol) {
    EXPECT_EQ(static_cast<std::uint8_t>(MotorError::NoError), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(MotorError::OverTemperature), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(MotorError::OverCurrent), 2);
    EXPECT_EQ(static_cast<std::uint8_t>(MotorError::VoltageHigh), 3);
    EXPECT_EQ(static_cast<std::uint8_t>(MotorError::VoltageLow), 4);
    EXPECT_EQ(static_cast<std::uint8_t>(MotorError::EncoderError), 5);
    EXPECT_EQ(static_cast<std::uint8_t>(MotorError::BrakeVoltageHigh), 6);
    EXPECT_EQ(static_cast<std::uint8_t>(MotorError::DriverError), 7);
    EXPECT_EQ(static_cast<std::uint8_t>(MotorError::OverTemperatureWarning), 8);
}

TEST(MotorEncodingTests, FloatBitsPreservesProtocolObjectRepresentation) {
    constexpr std::uint32_t kPatterns[] = {
        0x00000000u, 0x80000000u, 0x3f800000u, 0xc0200000u, 0x7f800000u, 0xff800000u, 0x7fc12345u,
    };

    for (const auto pattern : kPatterns) {
        float value = 0.0f;
        std::memcpy(&value, &pattern, sizeof(value));
        const auto bits = FloatBits(value);
        EXPECT_EQ(bits, pattern);

        const std::uint8_t wire[] = {
            static_cast<std::uint8_t>(pattern >> 24),
            static_cast<std::uint8_t>(pattern >> 16),
            static_cast<std::uint8_t>(pattern >> 8),
            static_cast<std::uint8_t>(pattern),
        };
        EXPECT_EQ(FloatBits(read_float_be(wire)), pattern);
    }
}

TEST(MotorEncodingTests, ConcurrentSpeedControlProducesStableFloatBytes) {
    constexpr int kThreadCount = 12;
    constexpr int kIterations = 200;
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);
    std::vector<std::vector<MotorMessage>> sent(kThreadCount);

    for (int thread_index = 0; thread_index < kThreadCount; ++thread_index) {
        workers.emplace_back([&, thread_index] {
            const auto name = "motor-concurrent-" + std::to_string(thread_index);
            auto adapter = MakeManagedAdapter<FakeAdapter>(name, name);
            adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
            auto motor = adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2);
            adapter->SetSyncMode(true);

            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int iteration = 0; iteration < kIterations; ++iteration) {
                const float speed = 0.25f * static_cast<float>(thread_index + 1) +
                                    0.001f * static_cast<float>(iteration);
                motor->SpdControl<0>(speed, 1.0f);
                adapter->Commit();
            }
            sent[thread_index] = adapter->GetRawSentMessages();
        });
    }

    start.store(true, std::memory_order_release);
    for (auto& worker : workers) {
        worker.join();
    }

    for (int thread_index = 0; thread_index < kThreadCount; ++thread_index) {
        ASSERT_EQ(sent[thread_index].size(), static_cast<std::size_t>(kIterations));
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            const float speed = 0.25f * static_cast<float>(thread_index + 1) +
                                0.001f * static_cast<float>(iteration);
            const float expected = speed * kRadiansPerSecondToRpm;
            const auto& packet = sent[thread_index][iteration].data;
            ASSERT_EQ(packet.len, 7);
            EXPECT_EQ(packet.data[0], 0x40);
            const float actual = read_float_be(packet.data + 1);
            const float tolerance = std::fabs(expected) * std::numeric_limits<float>::epsilon() * 8;
            EXPECT_NEAR(actual, expected, tolerance);
        }
    }
}

}  // namespace encos
