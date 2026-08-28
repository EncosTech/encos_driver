#include <gtest/gtest.h>

#include "test_fixtures.h"

namespace encos {

class BrakeTests : public MotorTestFixture {};

TEST_F(BrakeTests, BrakeEnableUsesFormattedCommandAndAutomaticAck) {
    EXPECT_TRUE(motor->Brake(true, true));

    const auto& payload = LastPayloadAs<FakeBrakePayload>(*adapter);
    EXPECT_TRUE(payload.enabled);
    EXPECT_TRUE(payload.wait_for_ack);

    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_TRUE(snapshot.brake_enabled);
}

TEST_F(BrakeTests, BrakeDisableManualModeTimesOut) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->Brake(false, true));
    const auto& payload = LastPayloadAs<FakeBrakePayload>(*adapter);
    EXPECT_FALSE(payload.enabled);
    EXPECT_TRUE(payload.wait_for_ack);
}

TEST_F(BrakeTests, BrakeEnableFailure) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->Brake(true, true));
}

TEST_F(BrakeTests, BrakeEnableNoWait) {
    EXPECT_TRUE(motor->Brake(true, false));
    const auto& payload = LastPayloadAs<FakeBrakePayload>(*adapter);
    EXPECT_TRUE(payload.enabled);
}

TEST_F(BrakeTests, BrakeEnableTimeout) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->Brake(true, true));
}

TEST_F(BrakeTests, BrakeDisableSuccess) {
    EXPECT_TRUE(motor->Brake(false, true));
    const auto& payload = LastPayloadAs<FakeBrakePayload>(*adapter);
    EXPECT_FALSE(payload.enabled);
    EXPECT_FALSE(adapter->GetMotorSnapshot(0, 1).brake_enabled);
}

TEST_F(BrakeTests, BrakeDisableFailure) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->Brake(false, true));
}

TEST_F(BrakeTests, BrakeDisableNoWait) {
    EXPECT_TRUE(motor->Brake(false, false));
    const auto& payload = LastPayloadAs<FakeBrakePayload>(*adapter);
    EXPECT_FALSE(payload.enabled);
}

TEST_F(BrakeTests, BrakeDisableTimeout) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->Brake(false, true));
}

}  // namespace encos
