#include <atomic>
#include <gtest/gtest.h>
#include <stdexcept>

#include "driver_manager_test_access.h"
#include "test_fixtures.h"

namespace encos {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

class CanIdTests : public MotorTestFixture {};

TEST_F(CanIdTests, SetIdProducesFormattedCommandAndAcknowledges) {
    EXPECT_TRUE(motor->SetId(5, true));

    const auto& payload = LastPayloadAs<FakeSetIdPayload>(*adapter);
    EXPECT_EQ(payload.source_id, 1);
    EXPECT_EQ(payload.target_id, 5);
    EXPECT_TRUE(payload.wait_for_ack);
}

TEST_F(CanIdTests, MotorFrameFlagApiAppliesToSentMessages) {
    motor->EnableCanFd();
    motor->EnableCanEff();
    EXPECT_TRUE(motor->SetId(5, false));
    EXPECT_EQ(LastCommand(*adapter).raw_frame_flags,
              static_cast<uint8_t>(kCanFrameFlagEff | kCanFrameFlagFdMask));
}

TEST_F(CanIdTests, SetIdFailure) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->SetId(5, true));
    const auto& payload = LastPayloadAs<FakeSetIdPayload>(*adapter);
    EXPECT_EQ(payload.source_id, 1);
    EXPECT_EQ(payload.target_id, 5);
}

TEST_F(CanIdTests, SetIdNoWait) {
    EXPECT_TRUE(motor->SetId(5, false));
    const auto& payload = LastPayloadAs<FakeSetIdPayload>(*adapter);
    EXPECT_EQ(payload.source_id, 1);
    EXPECT_EQ(payload.target_id, 5);
}

TEST_F(CanIdTests, SetIdNoWaitLeavesMotorAtItsOriginalIndex) {
    ASSERT_TRUE(motor->SetId(5, false));
    EXPECT_EQ(EncosDriverManager::Instance().FindMotor(bus, 1), motor);
    EXPECT_EQ(EncosDriverManager::Instance().FindMotor(bus, 5), nullptr);
}

TEST_F(CanIdTests, SetIdSuccess) {
    EXPECT_TRUE(motor->SetId(5, true));
}

TEST_F(CanIdTests, SetIdPreservesDirectStatusCallback) {
    std::atomic<unsigned> callbacks{0};
    motor->SetOnStatus([&](const MotorStatus&) {
        callbacks.fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_TRUE(motor->SetId(5, true));
    MotorStatus status{};
    status.error = MotorError::NoError;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 5, status, 1));
    EXPECT_EQ(callbacks.load(std::memory_order_relaxed), 1u);
}

TEST_F(CanIdTests, SetIdPrefersTargetPendingStatusCallback) {
    std::atomic<unsigned> source_callbacks{0};
    std::atomic<unsigned> target_callbacks{0};
    adapter->SetOnStatus(0, 1, [&](const MotorStatus&) {
        source_callbacks.fetch_add(1, std::memory_order_relaxed);
    });
    adapter->SetOnStatus(0, 5, [&](const MotorStatus&) {
        target_callbacks.fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_TRUE(motor->SetId(5, true));
    MotorStatus status{};
    status.error = MotorError::NoError;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 5, status, 1));
    EXPECT_EQ(source_callbacks.load(std::memory_order_relaxed), 0u);
    EXPECT_EQ(target_callbacks.load(std::memory_order_relaxed), 1u);
}

TEST_F(CanIdTests, SetIdMovesFeedbackRangeToNewId) {
    ASSERT_TRUE(motor->SetId(5, true));
    EXPECT_EQ(EncosDriverManager::Instance().FindMotor(bus, 1), nullptr);
    EXPECT_EQ(EncosDriverManager::Instance().FindMotor(bus, 5), motor);
    MotorStatus expected{};
    expected.error = MotorError::NoError;
    expected.position = 0.5F;
    auto feedback = adapter->MakeFeedbackMessage(0, 1, expected);
    feedback.data.id = 5;

    adapter->InjectMessage(feedback);

    EXPECT_TRUE(adapter->GetMotorStatus(0, 5, 0).has_value());
}

TEST_F(CanIdTests, SetIdRouteConflictLeavesTheManagedMotorAtItsOriginalIndex) {
    adapter->SeedMotor(0, 5, MotorModel::EC_A4310_P2);
    auto* conflicting_motor = bus->GetMotor(5, MotorModel::EC_A4310_P2);
    ASSERT_NE(conflicting_motor, nullptr);
    adapter->ClearCommandRecords();

    EXPECT_FALSE(motor->SetId(5, true));
    EXPECT_TRUE(adapter->GetCommandRecords().empty());
    EXPECT_EQ(EncosDriverManager::Instance().FindMotor(bus, 1), motor);
    EXPECT_EQ(EncosDriverManager::Instance().FindMotor(bus, 5), conflicting_motor);
}

TEST_F(CanIdTests, SetIdMigrationPreparationFailureDoesNotSendFirmwareCommand) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::SetMigrationHook(manager, [](EncosDriverManager::MigrationStage) {
        throw std::runtime_error("expected migration preparation failure");
    });
    adapter->ClearCommandRecords();

    EXPECT_FALSE(motor->SetId(5, true));
    DriverManagerTestAccess::SetMigrationHook(manager, {});
    EXPECT_TRUE(adapter->GetCommandRecords().empty());
    EXPECT_EQ(manager.FindMotor(bus, 1), motor);
    EXPECT_EQ(manager.FindMotor(bus, 5), nullptr);
}

TEST_F(CanIdTests, SetIdTimeout) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->SetId(5, true));
}

TEST_F(CanIdTests, SetPosProducesDecodedRadians) {
    EXPECT_TRUE(motor->SetPos(kPi / 2.0));

    const auto& payload = LastPayloadAs<FakeSetPosPayload>(*adapter);
    EXPECT_NEAR(payload.position_rad, static_cast<float>(kPi / 2.0), kDecodedAngleTolerance);
}

TEST_F(CanIdTests, GotoZeroUsesPositionReadThenSetPos) {
    auto seeded = adapter->GetMotorSnapshot(0, 1);
    seeded.position_rad = 1.5f;
    adapter->SeedMotor(0, 1, seeded);

    EXPECT_TRUE(motor->GotoZero(0.5f));

    const auto& commands = adapter->GetCommandRecords();
    ASSERT_EQ(commands.size(), 2);
    EXPECT_EQ(std::get<FakeGetParameterPayload>(commands[0].payload).parameter,
              MotorParameter::Position);
    const auto& payload = std::get<FakeSetPosPayload>(commands[1].payload);
    EXPECT_NEAR(payload.position_rad, 1.0f, kDecodedAngleTolerance);
}

TEST_F(CanIdTests, ResetZeroPosFailure) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->ResetZeroPos(true));
    const auto& commands = adapter->GetCommandRecords();
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::ResetZeroPos);
    EXPECT_TRUE(std::get<FakeResetZeroPosPayload>(commands[0].payload).is_legacy_reset);
}

TEST_F(CanIdTests, ResetZeroPosNoWait) {
    EXPECT_TRUE(motor->ResetZeroPos(false));
    adapter->Commit();
    const auto& commands = adapter->GetCommandRecords();
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::ResetZeroPos);
    EXPECT_TRUE(std::get<FakeResetZeroPosPayload>(commands[0].payload).is_legacy_reset);
}

TEST_F(CanIdTests, ResetZeroPosSuccess) {
    EXPECT_TRUE(motor->ResetZeroPos(true));
    const auto& commands = adapter->GetCommandRecords();
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::ResetZeroPos);
    EXPECT_TRUE(std::get<FakeResetZeroPosPayload>(commands[0].payload).is_legacy_reset);
}

TEST_F(CanIdTests, ResetZeroPosTimeout) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->ResetZeroPos(true));
    const auto& commands = adapter->GetCommandRecords();
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::ResetZeroPos);
}

TEST_F(CanIdTests, SetPosSendsCommandWithoutCanFdCapabilityProbe) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->SetPos(0.25));
    EXPECT_NEAR(LastPayloadAs<FakeSetPosPayload>(*adapter).position_rad, 0.25f,
                kDecodedAngleTolerance);
}

TEST_F(CanIdTests, SetPosSendsCentidegreePayloadAndReusesResetAck) {
    EXPECT_TRUE(motor->SetPos(kPi / 2.0));
    EXPECT_NEAR(LastPayloadAs<FakeSetPosPayload>(*adapter).position_rad,
                static_cast<float>(kPi / 2.0), kDecodedAngleTolerance);
}

TEST_F(CanIdTests, ResetZeroPosUsesLegacyResetCommandOnly) {
    EXPECT_TRUE(motor->ResetZeroPos(true));
    const auto& commands = adapter->GetCommandRecords();
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::ResetZeroPos);
    EXPECT_TRUE(std::get<FakeResetZeroPosPayload>(commands[0].payload).is_legacy_reset);
}

TEST_F(CanIdTests, GotoZeroFallsBackToLegacyControlWhenPositionQueryThrows) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    EXPECT_FALSE(motor->GotoZero(0.5f));
    const auto& commands = adapter->GetCommandRecords();
    ASSERT_FALSE(commands.empty());
    EXPECT_EQ(std::get<FakeGetParameterPayload>(commands.front().payload).parameter,
              MotorParameter::Position);
}

}  // namespace encos
