#include <gtest/gtest.h>

#include "test_fixtures.h"

namespace encos {

class StopTests : public MotorTestFixture {};

TEST_F(StopTests, StopNoResp1) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_EQ(motor->Stop<1>(MotorStopMode::DynamicBrake, 1.5f).error, MotorError::NoResponse);
    const auto& payload = LastPayloadAs<FakeStopPayload>(*adapter);
    EXPECT_EQ(payload.mode, MotorStopMode::DynamicBrake);
    EXPECT_NEAR(payload.current, 1.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 1);
}

TEST_F(StopTests, StopNoResp2) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_EQ(motor->Stop<2>(MotorStopMode::DynamicBrake, 1.5f).error, MotorError::NoResponse);
    EXPECT_EQ(LastPayloadAs<FakeStopPayload>(*adapter).feedback_type, 2);
}

TEST_F(StopTests, StopNoResp3) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_EQ(motor->Stop<3>(MotorStopMode::DynamicBrake, 1.5f).error, MotorError::NoResponse);
    EXPECT_EQ(LastPayloadAs<FakeStopPayload>(*adapter).feedback_type, 3);
}

TEST_F(StopTests, StopDecodesModeAndCurrent) {
    const auto result = motor->Stop<1>(MotorStopMode::DynamicBrake, 1.5f);

    const auto& payload = LastPayloadAs<FakeStopPayload>(*adapter);
    EXPECT_EQ(payload.mode, MotorStopMode::DynamicBrake);
    EXPECT_NEAR(payload.current, 1.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 1);
    EXPECT_EQ(result.error, MotorError::NoError);
}

TEST_F(StopTests, StopTest0) {
    motor->Stop<0>(MotorStopMode::DynamicBrake, 1.5f, 0);
    const auto& payload = LastPayloadAs<FakeStopPayload>(*adapter);
    EXPECT_EQ(payload.mode, MotorStopMode::DynamicBrake);
    EXPECT_NEAR(payload.current, 1.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 0);
}

TEST_F(StopTests, StopTest1) {
    EXPECT_EQ(motor->Stop<1>(MotorStopMode::FullBrake, 1.5f).error, MotorError::NoError);
    const auto& payload = LastPayloadAs<FakeStopPayload>(*adapter);
    EXPECT_EQ(payload.mode, MotorStopMode::FullBrake);
}

TEST_F(StopTests, StopTest2) {
    EXPECT_EQ(motor->Stop<2>(MotorStopMode::DynamicBrake, 1.5f).error, MotorError::NoError);
    const auto& payload = LastPayloadAs<FakeStopPayload>(*adapter);
    EXPECT_EQ(payload.mode, MotorStopMode::DynamicBrake);
}

TEST_F(StopTests, StopTest3) {
    EXPECT_EQ(motor->Stop<3>(MotorStopMode::RegenerativeBrake, 1.5f).error, MotorError::NoError);
    const auto& payload = LastPayloadAs<FakeStopPayload>(*adapter);
    EXPECT_EQ(payload.mode, MotorStopMode::RegenerativeBrake);
}

TEST_F(StopTests, StopOverCurrent) {
    auto snapshot = adapter->GetMotorSnapshot(0, 1);
    snapshot.error = MotorError::OverCurrent;
    adapter->SeedMotor(0, 1, snapshot);
    EXPECT_EQ(motor->Stop<1>(MotorStopMode::DynamicBrake, 1.5f).error, MotorError::OverCurrent);
}

TEST_F(StopTests, StopUsesRequestedMode) {
    motor->Stop<1>(MotorStopMode::RegenerativeBrake, 1.5f);
    EXPECT_EQ(LastPayloadAs<FakeStopPayload>(*adapter).mode, MotorStopMode::RegenerativeBrake);
}

}  // namespace encos
