#include <gtest/gtest.h>

#include "test_fixtures.h"

namespace encos {

class StatusLifecycleTests : public MotorTestFixture {};

TEST_F(StatusLifecycleTests, DefaultLifeCycleKeepsStatusAvailable) {
    MotorStatus status{};
    status.error = MotorError::NoError;
    status.position = 0.3f;
    status.speed = 0.2f;
    status.current = 0.1f;
    status.motor_temperature = 25.0f;
    status.mos_temperature = 30.0f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));

    for (int i = 0; i < 5; ++i) {
        const auto snapshot = adapter->GetMotorStatus(0, 1);
        ASSERT_TRUE(snapshot.has_value());
    }
}

TEST_F(StatusLifecycleTests, InjectedFeedbackParticipatesInLifecycleExpiry) {
    adapter->SetMaxStatusLifeCycle(2);

    MotorStatus status{};
    status.error = MotorError::NoError;
    status.position = 0.4f;
    status.speed = 0.3f;
    status.current = 0.2f;
    status.motor_temperature = 25.0f;
    status.mos_temperature = 30.0f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));

    ASSERT_TRUE(adapter->GetMotorStatus(0, 1).has_value());
    ASSERT_TRUE(adapter->GetMotorStatus(0, 1).has_value());
    EXPECT_FALSE(adapter->GetMotorStatus(0, 1).has_value());
}

TEST_F(StatusLifecycleTests, NewFeedbackResetsLifeCycleToMaxValue) {
    adapter->SetMaxStatusLifeCycle(2);

    MotorStatus status{};
    status.error = MotorError::NoError;
    status.position = 0.1f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));

    ASSERT_TRUE(adapter->GetMotorStatus(0, 1).has_value());
    ASSERT_TRUE(adapter->GetMotorStatus(0, 1).has_value());
    EXPECT_FALSE(adapter->GetMotorStatus(0, 1).has_value());

    status.position = 0.2f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));
    const auto refreshed = adapter->GetMotorStatus(0, 1);
    ASSERT_TRUE(refreshed.has_value());
    EXPECT_NEAR(refreshed->position, 0.2f, kDecodedAngleTolerance);
}

TEST_F(StatusLifecycleTests, FiniteLifeCycleClampsExistingDefaultStatus) {
    MotorStatus status{};
    status.error = MotorError::NoError;
    status.position = 0.6f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));

    adapter->SetMaxStatusLifeCycle(1);
    ASSERT_TRUE(adapter->GetMotorStatus(0, 1).has_value());
    EXPECT_FALSE(adapter->GetMotorStatus(0, 1).has_value());
}

TEST_F(StatusLifecycleTests, NonPositiveDeductionDoesNotConsumeLifeCycle) {
    adapter->SetMaxStatusLifeCycle(1);

    MotorStatus status{};
    status.error = MotorError::NoError;
    status.position = 0.5f;
    status.speed = 0.1f;
    status.current = 0.1f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));

    for (int i = 0; i < 3; ++i) {
        const auto snapshot = adapter->GetMotorStatus(0, 1, 0);
        ASSERT_TRUE(snapshot.has_value());
    }
}

TEST_F(StatusLifecycleTests, SnapshotReadsDoNotConsumeLifecycle) {
    adapter->SetMaxStatusLifeCycle(1);
    MotorStatus status{};
    status.error = MotorError::NoError;
    status.position = 1.0f;
    status.speed = 0.2f;
    status.current = 0.1f;
    status.motor_temperature = 25.0f;
    status.mos_temperature = 30.0f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));

    EXPECT_EQ(adapter->GetMotorStatus().size(), 1);
    EXPECT_EQ(adapter->GetMotorStatus().size(), 1);
    ASSERT_TRUE(adapter->GetMotorStatus(0, 1).has_value());
    EXPECT_FALSE(adapter->GetMotorStatus(0, 1).has_value());
}

}  // namespace encos
