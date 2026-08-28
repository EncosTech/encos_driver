#include <gtest/gtest.h>

#include "test_fixtures.h"

namespace encos {

class PVTControlTests : public MotorTestFixture {};

TEST_F(PVTControlTests, PVTControlClampsIntoDecodedCommandRecord) {
    motor->PVTControl<0>(600.0f, -1.0f, 15.0f, -20.0f, 40.0f);

    ExpectCommandKind(*adapter, FakeCommandKind::PVTControl);
    const auto& payload = LastPayloadAs<FakePVTControlPayload>(*adapter);
    EXPECT_NEAR(payload.kp, 500.0f, kDecodedFloatTolerance);
    EXPECT_NEAR(payload.kd, 0.0f, kDecodedFloatTolerance);
    EXPECT_NEAR(payload.position, 12.5f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.speed, -18.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.torque, 30.0f, kDecodedFloatTolerance);
}

TEST_F(PVTControlTests, PVTControlManualModeReturnsNoResponse) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    const auto result = motor->PVTControl<1>(10.0f, 1.0f, 0.5f, 1.0f, 5.0f);
    EXPECT_EQ(result.error, MotorError::NoResponse);
    ExpectCommandKind(*adapter, FakeCommandKind::PVTControl);
}

TEST_F(PVTControlTests, PVTControlAutomaticFeedbackUpdatesSnapshot) {
    const auto result = motor->PVTControl<1>(10.0f, 1.0f, 0.5f, 1.0f, 5.0f);

    EXPECT_EQ(result.error, MotorError::NoError);
    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot.position_rad, 0.5f, kDecodedAngleTolerance);
    EXPECT_NEAR(snapshot.speed_rad_s, 1.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(snapshot.current_a, 5.0f, kDecodedFloatTolerance);
}

TEST_F(PVTControlTests, PVTControlOverCurrentUsesSnapshotError) {
    auto snapshot = adapter->GetMotorSnapshot(0, 1);
    snapshot.error = MotorError::OverCurrent;
    adapter->SeedMotor(0, 1, snapshot);

    const auto result = motor->PVTControl<1>(10.0f, 1.0f, 0.5f, 1.0f, 5.0f);

    EXPECT_EQ(result.error, MotorError::OverCurrent);
    ExpectCommandKind(*adapter, FakeCommandKind::PVTControl);
}

}  // namespace encos
