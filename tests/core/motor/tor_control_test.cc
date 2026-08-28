#include <gtest/gtest.h>

#include "test_fixtures.h"

namespace encos {

class TorControlTests : public MotorTestFixture {};

TEST_F(TorControlTests, TorControlNoResp1) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_EQ(motor->TorControl<1>(3.5f).error, MotorError::NoResponse);
    const auto& payload = LastPayloadAs<FakeTorControlPayload>(*adapter);
    EXPECT_NEAR(payload.torque, 3.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 1);
}

TEST_F(TorControlTests, TorControlNoResp2) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_EQ(motor->TorControl<2>(3.5f).error, MotorError::NoResponse);
    EXPECT_EQ(LastPayloadAs<FakeTorControlPayload>(*adapter).feedback_type, 2);
}

TEST_F(TorControlTests, TorControlNoResp3) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_EQ(motor->TorControl<3>(3.5f).error, MotorError::NoResponse);
    EXPECT_EQ(LastPayloadAs<FakeTorControlPayload>(*adapter).feedback_type, 3);
}

TEST_F(TorControlTests, TorControlDecodesFormattedCommand) {
    const auto result = motor->TorControl<1>(3.5f);

    const auto& payload = LastPayloadAs<FakeTorControlPayload>(*adapter);
    EXPECT_NEAR(payload.torque, 3.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 1);
    EXPECT_EQ(result.error, MotorError::NoError);
}

TEST_F(TorControlTests, TorControlTest0) {
    motor->TorControl<0>(3.5f, 0);
    const auto& payload = LastPayloadAs<FakeTorControlPayload>(*adapter);
    EXPECT_NEAR(payload.torque, 3.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 0);
}

TEST_F(TorControlTests, TorControlTest1) {
    EXPECT_EQ(motor->TorControl<1>(3.5f).error, MotorError::NoError);
    const auto& payload = LastPayloadAs<FakeTorControlPayload>(*adapter);
    EXPECT_NEAR(payload.torque, 3.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 1);
}

TEST_F(TorControlTests, TorControlTest2) {
    EXPECT_EQ(motor->TorControl<2>(3.5f).error, MotorError::NoError);
    const auto& payload = LastPayloadAs<FakeTorControlPayload>(*adapter);
    EXPECT_NEAR(payload.torque, 3.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 2);
}

TEST_F(TorControlTests, TorControlTest3) {
    EXPECT_EQ(motor->TorControl<3>(3.5f).error, MotorError::NoError);
    const auto& payload = LastPayloadAs<FakeTorControlPayload>(*adapter);
    EXPECT_NEAR(payload.torque, 3.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 3);
}

TEST_F(TorControlTests, TorControlOverCurrent) {
    auto snapshot = adapter->GetMotorSnapshot(0, 1);
    snapshot.error = MotorError::OverCurrent;
    adapter->SeedMotor(0, 1, snapshot);
    EXPECT_EQ(motor->TorControl<1>(3.5f).error, MotorError::OverCurrent);
}

TEST_F(TorControlTests, TorControlFeedbackType3DecodesTorqueAndUpdatesSnapshot) {
    const auto result = motor->TorControl<3>(3.5f);

    EXPECT_EQ(result.error, MotorError::NoError);
    const auto& payload = LastPayloadAs<FakeTorControlPayload>(*adapter);
    EXPECT_EQ(payload.feedback_type, 3);
    EXPECT_NEAR(payload.torque, 3.5f, kDecodedFloatTolerance);

    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot.torque_nm, payload.torque, kDecodedFloatTolerance);
    EXPECT_NEAR(snapshot.current_a, payload.torque, kDecodedFloatTolerance);
}

}  // namespace encos
