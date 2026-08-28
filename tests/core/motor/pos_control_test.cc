#include <gtest/gtest.h>

#include "test_fixtures.h"

namespace encos {

class PosControlTests : public MotorTestFixture {};

TEST_F(PosControlTests, PosControlNoResp1) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_EQ(motor->PosControl<1>(0.5f, 1.0f, 1.0f).error, MotorError::NoResponse);
    const auto& payload = LastPayloadAs<FakePosControlPayload>(*adapter);
    EXPECT_NEAR(payload.position, 0.5f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.speed, 1.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.current, 1.0f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 1);
}

TEST_F(PosControlTests, PosControlNoResp2) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_EQ(motor->PosControl<2>(0.5f, 1.0f, 1.0f).error, MotorError::NoResponse);
    EXPECT_EQ(LastPayloadAs<FakePosControlPayload>(*adapter).feedback_type, 2);
}

TEST_F(PosControlTests, PosControlNoResp3) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_EQ(motor->PosControl<3>(0.5f, 1.0f, 1.0f).error, MotorError::NoResponse);
    EXPECT_EQ(LastPayloadAs<FakePosControlPayload>(*adapter).feedback_type, 3);
}

TEST_F(PosControlTests, PosControlDecodesPayloadAndReturnsAutoFeedback) {
    const auto result = motor->PosControl<2>(0.75f, 1.5f, 2.5f);

    const auto& payload = LastPayloadAs<FakePosControlPayload>(*adapter);
    EXPECT_NEAR(payload.position, 0.75f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.speed, 1.5f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.current, 2.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 2);

    EXPECT_EQ(result.error, MotorError::NoError);
    EXPECT_NEAR(result.position, payload.position, kDecodedAngleTolerance);
    EXPECT_NEAR(result.current, payload.current, kDecodedFloatTolerance);
}

TEST_F(PosControlTests, PosControlFeedbackMismatchStillThrows) {
    EXPECT_THROW((void) motor->PosControl<1>(0.5f, 1.0f, 1.0f, 2), std::invalid_argument);
}

TEST_F(PosControlTests, PosControlTest1) {
    const auto result = motor->PosControl<1>(0.75f, 1.5f, 2.5f);
    EXPECT_EQ(result.error, MotorError::NoError);
    const auto& payload = LastPayloadAs<FakePosControlPayload>(*adapter);
    EXPECT_NEAR(payload.position, 0.75f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.speed, 1.5f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.current, 2.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 1);
}

TEST_F(PosControlTests, PosControlTest2) {
    const auto result = motor->PosControl<2>(0.75f, 1.5f, 2.5f);
    EXPECT_EQ(result.error, MotorError::NoError);
    const auto& payload = LastPayloadAs<FakePosControlPayload>(*adapter);
    EXPECT_NEAR(payload.position, 0.75f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.speed, 1.5f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.current, 2.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 2);
}

TEST_F(PosControlTests, PosControlTest3) {
    const auto result = motor->PosControl<3>(0.75f, 1.5f, 2.5f);
    EXPECT_EQ(result.error, MotorError::NoError);
    const auto& payload = LastPayloadAs<FakePosControlPayload>(*adapter);
    EXPECT_NEAR(payload.position, 0.75f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.speed, 1.5f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.current, 2.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 3);
}

TEST_F(PosControlTests, PosControlTest0) {
    motor->PosControl<0>(0.75f, 1.5f, 2.5f, 0);
    const auto& payload = LastPayloadAs<FakePosControlPayload>(*adapter);
    EXPECT_NEAR(payload.position, 0.75f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.speed, 1.5f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.current, 2.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 0);
}

}  // namespace encos
