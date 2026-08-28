#include <gtest/gtest.h>

#include "test_fixtures.h"

namespace encos {

class CurControlTests : public MotorTestFixture {};

TEST_F(CurControlTests, CurControlNoResp1) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_EQ(motor->CurControl<1>(2.25f).error, MotorError::NoResponse);
    const auto& payload = LastPayloadAs<FakeCurControlPayload>(*adapter);
    EXPECT_NEAR(payload.current, 2.25f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 1);
}

TEST_F(CurControlTests, CurControlNoResp2) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_EQ(motor->CurControl<2>(2.25f).error, MotorError::NoResponse);
    EXPECT_EQ(LastPayloadAs<FakeCurControlPayload>(*adapter).feedback_type, 2);
}

TEST_F(CurControlTests, CurControlNoResp3) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_EQ(motor->CurControl<3>(2.25f).error, MotorError::NoResponse);
    EXPECT_EQ(LastPayloadAs<FakeCurControlPayload>(*adapter).feedback_type, 3);
}

TEST_F(CurControlTests, CurControlDecodesFormattedCommand) {
    const auto result = motor->CurControl<2>(2.25f);

    const auto& payload = LastPayloadAs<FakeCurControlPayload>(*adapter);
    EXPECT_NEAR(payload.current, 2.25f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 2);
    EXPECT_EQ(result.error, MotorError::NoError);
    EXPECT_NEAR(result.current, payload.current, kDecodedFloatTolerance);
}

TEST_F(CurControlTests, CurControlTest0) {
    motor->CurControl<0>(2.25f, 0);
    const auto& payload = LastPayloadAs<FakeCurControlPayload>(*adapter);
    EXPECT_NEAR(payload.current, 2.25f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 0);
}

TEST_F(CurControlTests, CurControlTest1) {
    EXPECT_EQ(motor->CurControl<1>(2.25f).error, MotorError::NoError);
    const auto& payload = LastPayloadAs<FakeCurControlPayload>(*adapter);
    EXPECT_NEAR(payload.current, 2.25f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 1);
}

TEST_F(CurControlTests, CurControlTest2) {
    EXPECT_EQ(motor->CurControl<2>(2.25f).error, MotorError::NoError);
    const auto& payload = LastPayloadAs<FakeCurControlPayload>(*adapter);
    EXPECT_NEAR(payload.current, 2.25f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 2);
}

TEST_F(CurControlTests, CurControlTest3) {
    EXPECT_EQ(motor->CurControl<3>(2.25f).error, MotorError::NoError);
    const auto& payload = LastPayloadAs<FakeCurControlPayload>(*adapter);
    EXPECT_NEAR(payload.current, 2.25f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 3);
}

TEST_F(CurControlTests, CurControlOverCurrent) {
    auto snapshot = adapter->GetMotorSnapshot(0, 1);
    snapshot.error = MotorError::OverCurrent;
    adapter->SeedMotor(0, 1, snapshot);

    EXPECT_EQ(motor->CurControl<3>(2.25f).error, MotorError::OverCurrent);
    EXPECT_NEAR(LastPayloadAs<FakeCurControlPayload>(*adapter).current, 2.25f,
                kDecodedFloatTolerance);
}

}  // namespace encos
