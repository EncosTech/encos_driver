#include <chrono>
#include <gtest/gtest.h>
#include <limits>
#include <thread>

#include "test_fixtures.h"

namespace {
constexpr double kPi = 3.14159265358979323846;
}

namespace encos {

class ControlParametersTests : public MotorTestFixture {};

TEST_F(ControlParametersTests, ResetZeroPosFailure) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->ResetZeroPos(true));
    const auto& commands = adapter->GetCommandRecords();
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::ResetZeroPos);
    EXPECT_TRUE(std::get<FakeResetZeroPosPayload>(commands[0].payload).is_legacy_reset);
}

TEST_F(ControlParametersTests, ResetZeroPosNoWait) {
    EXPECT_TRUE(motor->ResetZeroPos(false));
    adapter->Commit();
    const auto& commands = adapter->GetCommandRecords();
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::ResetZeroPos);
    EXPECT_TRUE(std::get<FakeResetZeroPosPayload>(commands[0].payload).is_legacy_reset);
}

TEST_F(ControlParametersTests, ResetZeroPosSuccess) {
    EXPECT_TRUE(motor->ResetZeroPos(true));
    const auto& commands = adapter->GetCommandRecords();
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::ResetZeroPos);
    EXPECT_TRUE(std::get<FakeResetZeroPosPayload>(commands[0].payload).is_legacy_reset);
}

TEST_F(ControlParametersTests, ResetZeroPosTimeout) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->ResetZeroPos(true));
    const auto& commands = adapter->GetCommandRecords();
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::ResetZeroPos);
}

TEST_F(ControlParametersTests, SetPosOutOfRangeThrows) {
    constexpr double kLargePos =
        static_cast<double>(std::numeric_limits<int16_t>::max()) * kPi / 180.0 / 100.0;
    EXPECT_THROW(motor->SetPos(kLargePos + 1.0), std::out_of_range);
}

TEST_F(ControlParametersTests, SetPosMinimumValidValue) {
    EXPECT_TRUE(motor->SetPos(0.0));
    EXPECT_NEAR(LastPayloadAs<FakeSetPosPayload>(*adapter).position_rad, 0.0f,
                kDecodedAngleTolerance);
}

TEST_F(ControlParametersTests, SetAccelerationFailure) {
    adapter->SetParameterWritePolicy(0, 1, MotorParameter::Acceleration, FakeWritePolicy::Ignore);
    EXPECT_FALSE(motor->SetAcceleration(5.0f, true));
    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.parameter, MotorParameter::Acceleration);
    EXPECT_TRUE(payload.wait_for_ack);
    ExpectRawBytes(payload.raw_value, {0x01, 0xF4});
}

TEST_F(ControlParametersTests, SetAccelerationNoWait) {
    EXPECT_TRUE(motor->SetAcceleration(5.0f, false));
    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.parameter, MotorParameter::Acceleration);
    EXPECT_FALSE(payload.wait_for_ack);
    ExpectRawBytes(payload.raw_value, {0x01, 0xF4});
}

TEST_F(ControlParametersTests, SetAccelerationSuccess) {
    EXPECT_TRUE(motor->SetAcceleration(5.0f, true));
    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.parameter, MotorParameter::Acceleration);
    EXPECT_TRUE(payload.wait_for_ack);
    ExpectRawBytes(payload.raw_value, {0x01, 0xF4});
}

TEST_F(ControlParametersTests, SetAccelerationTimeout) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->SetAcceleration(5.0f, true));
}

TEST_F(ControlParametersTests, SetAccelerationAndGetAccelerationUseFakeSnapshot) {
    EXPECT_TRUE(motor->SetAcceleration(5.0f, true));
    const auto& write_payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(write_payload.parameter, MotorParameter::Acceleration);
    EXPECT_TRUE(write_payload.wait_for_ack);
    ExpectRawBytes(write_payload.raw_value, {0x01, 0xF4});

    const float acceleration = motor->GetParameter<MotorParameter::Acceleration>();
    EXPECT_NEAR(acceleration, 5.0f, kDecodedFloatTolerance);
    const auto& read_payload = LastPayloadAs<FakeGetParameterPayload>(*adapter);
    EXPECT_EQ(read_payload.parameter, MotorParameter::Acceleration);
}

TEST_F(ControlParametersTests, SetCommunicationModeIsRecordedAsRawParameterWrite) {
    EXPECT_TRUE(motor->SetCommunicationMode(MotorCommunicationMode::CanFd, false));
    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.raw_parameter_id, 0x02);
    EXPECT_FALSE(payload.wait_for_ack);
    ExpectRawBytes(payload.raw_value, {0x01});
}

TEST_F(ControlParametersTests, IgnoredCanTimeoutWriteReturnsFalse) {
    adapter->SetParameterWritePolicy(0, 1, MotorParameter::CanTimeout, FakeWritePolicy::Ignore);
    EXPECT_FALSE(motor->SetCanTimeout(1000, true));
    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.parameter, MotorParameter::CanTimeout);
    EXPECT_FALSE(payload.wait_for_ack);
    ExpectRawBytes(payload.raw_value, {0x03, 0xE8});
}

#define EXPECT_WRITE_SCENARIO(test_name, expr, parameter_id, b0, b1, b2, b3)    \
    TEST_F(ControlParametersTests, test_name##Failure) {                        \
        adapter->SetReplyMode(FakeReplyMode::Manual);                           \
        EXPECT_FALSE((expr));                                                   \
        const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter); \
        EXPECT_EQ(payload.parameter, parameter_id);                             \
        EXPECT_FALSE(payload.wait_for_ack);                                     \
        ExpectRawBytes(payload.raw_value, {b0, b1, b2, b3});                    \
    }                                                                           \
    TEST_F(ControlParametersTests, test_name##NoWait) {                         \
        EXPECT_TRUE((expr));                                                    \
        const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter); \
        EXPECT_EQ(payload.parameter, parameter_id);                             \
        EXPECT_FALSE(payload.wait_for_ack);                                     \
        ExpectRawBytes(payload.raw_value, {b0, b1, b2, b3});                    \
    }                                                                           \
    TEST_F(ControlParametersTests, test_name##Success) {                        \
        EXPECT_TRUE((expr));                                                    \
        const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter); \
        EXPECT_EQ(payload.parameter, parameter_id);                             \
        EXPECT_FALSE(payload.wait_for_ack);                                     \
        ExpectRawBytes(payload.raw_value, {b0, b1, b2, b3});                    \
    }                                                                           \
    TEST_F(ControlParametersTests, test_name##Timeout) {                        \
        adapter->SetReplyMode(FakeReplyMode::Manual);                           \
        EXPECT_FALSE((expr));                                                   \
        const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter); \
        EXPECT_EQ(payload.parameter, parameter_id);                             \
        EXPECT_FALSE(payload.wait_for_ack);                                     \
        ExpectRawBytes(payload.raw_value, {b0, b1, b2, b3});                    \
    }

EXPECT_WRITE_SCENARIO(SetCurPI, motor->SetCurPI(1.0f, 0.1f, true), MotorParameter::CurKpKi, 0x27,
                      0x10, 0x00, 0x01)
EXPECT_WRITE_SCENARIO(SetSpdPI, motor->SetSpdPI(2.0f, 0.2f, true), MotorParameter::SpdKpKi, 0xFF,
                      0xFF, 0x4E, 0x20)
EXPECT_WRITE_SCENARIO(SetPosPD, motor->SetPosPD(3.0f, 0.3f, true), MotorParameter::PosKpKd, 0xFF,
                      0xFF, 0x75, 0x30)

TEST_F(ControlParametersTests, SetCanTimeoutFailure) {
    adapter->SetParameterWritePolicy(0, 1, MotorParameter::CanTimeout, FakeWritePolicy::Ignore);
    EXPECT_FALSE(motor->SetCanTimeout(1000, true));
    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.parameter, MotorParameter::CanTimeout);
    EXPECT_FALSE(payload.wait_for_ack);
    ExpectRawBytes(payload.raw_value, {0x03, 0xE8});
}

TEST_F(ControlParametersTests, SetCanTimeoutNoWait) {
    EXPECT_TRUE(motor->SetCanTimeout(1000, false));
    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.parameter, MotorParameter::CanTimeout);
    EXPECT_FALSE(payload.wait_for_ack);
    ExpectRawBytes(payload.raw_value, {0x03, 0xE8});
}

TEST_F(ControlParametersTests, SetCanTimeoutSuccess) {
    EXPECT_TRUE(motor->SetCanTimeout(1000, true));
    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.parameter, MotorParameter::CanTimeout);
    EXPECT_FALSE(payload.wait_for_ack);
    ExpectRawBytes(payload.raw_value, {0x03, 0xE8});
}

TEST_F(ControlParametersTests, SetCanTimeoutTimeout) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->SetCanTimeout(1000, true));
}

TEST_F(ControlParametersTests, SetCommunicationModeCanFdSuccess) {
    EXPECT_TRUE(motor->SetCommunicationMode(MotorCommunicationMode::CanFd, true));
    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.raw_parameter_id, 0x02);
    EXPECT_TRUE(payload.wait_for_ack);
    ExpectRawBytes(payload.raw_value, {0x01});
}

TEST_F(ControlParametersTests, SetCommunicationModeNoWait) {
    EXPECT_TRUE(motor->SetCommunicationMode(MotorCommunicationMode::ClassicCan, false));
    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.raw_parameter_id, 0x02);
    EXPECT_FALSE(payload.wait_for_ack);
    ExpectRawBytes(payload.raw_value, {0x00});
}

TEST_F(ControlParametersTests, SetCommunicationModeTimeout) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->SetCommunicationMode(MotorCommunicationMode::CanFd, true));
}

TEST_F(ControlParametersTests, SetCommunicationModeAckWithWrongPayloadReturnsFalse) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    std::thread injector([adapter = adapter]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        MotorPackMsg ack{};
        ack.id = 1;
        ack.len = 4;
        ack.data[0] = 0xFF;
        ack.data[1] = 0xFE;
        ack.data[2] = 0x02;
        ack.data[3] = 0x00;
        adapter->InjectMessage(MotorMessage{0, ack});
    });

    EXPECT_FALSE(motor->SetCommunicationMode(MotorCommunicationMode::CanFd, true));
    injector.join();

    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.raw_parameter_id, 0x02);
    ExpectRawBytes(payload.raw_value, {0x01});
}

TEST_F(ControlParametersTests, SetCommunicationModeClassicCanAckSucceeds) {
    EXPECT_TRUE(motor->SetCommunicationMode(MotorCommunicationMode::ClassicCan, true));
    const auto& payload = LastPayloadAs<FakeSetParameterPayload>(*adapter);
    EXPECT_EQ(payload.raw_parameter_id, 0x02);
    EXPECT_TRUE(payload.wait_for_ack);
    ExpectRawBytes(payload.raw_value, {0x00});
}

TEST_F(ControlParametersTests, SetCommunicationModeRejectsInvalidMode) {
    EXPECT_THROW(
        (void) motor->SetCommunicationMode(static_cast<MotorCommunicationMode>(0x03), false),
        std::invalid_argument);
}

TEST_F(ControlParametersTests, SetCommunicationModeDoesNotChangeLocalCanFdFlag) {
    EXPECT_FALSE(motor->IsCanFdEnabled());
    EXPECT_TRUE(motor->SetCommunicationMode(MotorCommunicationMode::CanFd, true));
    EXPECT_FALSE(motor->IsCanFdEnabled());
}

TEST_F(ControlParametersTests, GetAccelerationTimeout) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_THROW((void) motor->GetParameter<MotorParameter::Acceleration>(), std::runtime_error);
}

#undef EXPECT_WRITE_SCENARIO

}  // namespace encos
