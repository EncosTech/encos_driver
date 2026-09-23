// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

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

TEST_F(BrakeTests, CommandsUseSection914AndMechanicalBrakeSemantics) {
    for (const bool enabled : {true, false}) {
        EXPECT_TRUE(motor->Brake(enabled));
        const auto messages = adapter->GetRawSentMessages();
        ASSERT_FALSE(messages.empty());
        const auto& pack = messages.back().data;
        EXPECT_EQ(pack.id, 1u);
        EXPECT_EQ(pack.len, 3);
        EXPECT_EQ(pack.data[0], 0x75);
        EXPECT_EQ(pack.data[1], 0);
        EXPECT_EQ(pack.data[2], enabled ? 0 : 1);
    }
}

TEST_F(BrakeTests, AcceptsManualTwoByteAcknowledgements) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    for (const uint8_t header : {0xA0, 0xC0}) {
        for (const bool enabled : {true, false}) {
            adapter->SetDecodedCommandObserver([&, enabled, header](const FakeCommandRecord&) {
                MotorPackMsg ack{};
                ack.id = 1;
                ack.len = 2;
                ack.data[0] = header;
                ack.data[1] = enabled ? 0 : 1;
                adapter->InjectMessage(MotorMessage{0, ack});
            });
            EXPECT_TRUE(motor->Brake(enabled));
        }
    }
    adapter->ClearDecodedCommandObserver();
}

TEST_F(BrakeTests, RejectsMismatchedTruncatedAndOtherPathAcknowledgements) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    for (int scenario = 0; scenario < 4; ++scenario) {
        adapter->SetDecodedCommandObserver([&, scenario](const FakeCommandRecord&) {
            MotorPackMsg ack{};
            ack.id = 1;
            ack.len = scenario == 1 ? 1 : 2;
            ack.data[0] = scenario == 2 ? 0xB2 : (scenario == 3 ? 0xC6 : 0xC0);
            ack.data[1] = scenario == 0 ? 1 : 0;
            adapter->InjectMessage(MotorMessage{0, ack});
        });
        EXPECT_FALSE(motor->Brake(true));
    }
    adapter->ClearDecodedCommandObserver();
}

TEST_F(BrakeTests, RejectsLegacyWrongStateErrorsAndLongReplies) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    for (const uint8_t header : {0xA0, 0xC0}) {
        for (int scenario = 0; scenario < 5; ++scenario) {
            adapter->SetDecodedCommandObserver([&, header, scenario](const FakeCommandRecord&) {
                MotorPackMsg ack{};
                ack.id = 1;
                ack.len = scenario == 0 ? 1 : (scenario >= 3 ? 3 : 2);
                ack.data[0] = scenario == 2 ? header | 6 : header;
                ack.data[1] = scenario == 1 ? 1 : (scenario == 4 ? 0x25 : 0);
                adapter->InjectMessage(MotorMessage{0, ack});
            });
            EXPECT_FALSE(motor->Brake(true));
        }
    }
    adapter->ClearDecodedCommandObserver();
}

TEST_F(BrakeTests, StatusQueryAcceptsSingleByteProtocolStates) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    for (const uint8_t state : {0, 1}) {
        adapter->SetDecodedCommandObserver([&, state](const FakeCommandRecord&) {
            MotorPackMsg reply{};
            reply.id = 1;
            reply.len = 3;
            reply.data[0] = 0xA0;
            reply.data[1] = 0x25;
            reply.data[2] = state;
            adapter->InjectMessage(MotorMessage{0, reply});
        });
        EXPECT_EQ(motor->GetParameter<MotorParameter::BrakeStatus>(), state);
    }
    adapter->ClearDecodedCommandObserver();
}

TEST_F(BrakeTests, StatusQueryRejectsMissingState) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    adapter->SetDecodedCommandObserver([&](const FakeCommandRecord&) {
        MotorPackMsg reply{};
        reply.id = 1;
        reply.len = 2;
        reply.data[0] = 0xA0;
        reply.data[1] = 0x25;
        adapter->InjectMessage(MotorMessage{0, reply});
    });
    EXPECT_THROW(motor->GetParameter<MotorParameter::BrakeStatus>(), std::runtime_error);
    adapter->ClearDecodedCommandObserver();
}

TEST_F(BrakeTests, FakeStatusReflectsProtocolState) {
    EXPECT_TRUE(motor->Brake(true));
    EXPECT_EQ(motor->GetParameter<MotorParameter::BrakeStatus>(), 0);
    EXPECT_TRUE(motor->Brake(false));
    EXPECT_EQ(motor->GetParameter<MotorParameter::BrakeStatus>(), 1);
}

}  // namespace encos
