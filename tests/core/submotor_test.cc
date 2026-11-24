// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <cstring>
#include <future>
#include <gtest/gtest.h>

#include "base_adapter_test_access.h"
#include "bus/bus.h"
#include "bus/submotor_packet.h"
#include "device/motor/motor.h"
#include "driver_manager_test_access.h"
#include "encos/driver_manager.h"
#include "test_adapter.h"

namespace encos {
namespace {

MotorPackMsg Slot(CanId can_id, unsigned sub_id, uint8_t value, uint8_t length = 8) {
    MotorPackMsg message{};
    message.id = MakeCanAddress(can_id, sub_id);
    message.len = length;
    std::fill_n(message.data, length, value);
    return message;
}

class SubMotorSendTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& manager = EncosDriverManager::Instance();
        DriverManagerTestAccess::Reset(manager);
        adapter = static_cast<TestAdapter*>(manager.CreateAdapterWithFactory("submotor-send", [] {
            return new TestAdapter("submotor-send");
        }));
        bus = adapter->GetBus(0);
    }
    void TearDown() override {
        DriverManagerTestAccess::Reset(EncosDriverManager::Instance());
    }
    TestAdapter* adapter = nullptr;
    Bus* bus = nullptr;
};

TEST_F(SubMotorSendTest, DirectCommandsProduceIndependentZeroFilledPackets) {
    auto writer = BaseAdapterTestAccess::MakeDeviceWriter(adapter, bus);
    for (unsigned sub = 1; sub <= 7; ++sub) {
        writer(Slot(0x12345, sub, static_cast<uint8_t>(sub), 3));
    }
    const auto sent = adapter->GetSentMessages();
    ASSERT_EQ(sent.size(), 7U);
    for (unsigned sub = 1; sub <= 7; ++sub) {
        const auto& message = sent[sub - 1].data;
        EXPECT_EQ(message.id, 0x12345U);
        EXPECT_EQ(message.len, sub == 7 ? 63U : 54U);
        EXPECT_TRUE(CanFrameFlagsUseCanFd(message.frame_flags));
        EXPECT_TRUE(CanFrameFlagsUseExtendedId(message.frame_flags));
        for (unsigned byte = 0; byte < 64; ++byte) {
            const bool active = byte > (sub - 1) * 9 && byte <= (sub - 1) * 9 + 3;
            EXPECT_EQ(message.data[byte], byte == (sub - 1) * 9 ? 3 : (active ? sub : 0));
        }
    }
}

TEST_F(SubMotorSendTest, RepeatedSlotsExpandAndEveryFlushPathPackagesCommands) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        adapter->ClearSentMessages();
        auto first = BaseAdapterTestAccess::MakeDeviceWriter(adapter, bus);
        auto second = BaseAdapterTestAccess::MakeDeviceWriter(adapter, bus);
        auto seventh = BaseAdapterTestAccess::MakeDeviceWriter(adapter, bus);
        bus->SetSyncMode(true);
        first(Slot(100, 1, 11));
        first(Slot(100, 1, 12));
        second(Slot(100, 2, 21));
        seventh(Slot(100, 7, 71));
        seventh(Slot(100, 7, 72));
        EXPECT_TRUE(adapter->GetSentMessages().empty());
        if (mode == 0)
            bus->Commit();
        if (mode == 1)
            adapter->Commit();
        if (mode == 2)
            bus->SetSyncMode(false);
        if (mode == 3)
            adapter->SetSyncMode(false);
        const auto sent = adapter->GetSentMessages();
        ASSERT_EQ(sent.size(), 2U) << mode;
        for (unsigned round = 0; round < 2; ++round) {
            EXPECT_EQ(sent[round].data.id, 100U);
            EXPECT_EQ(sent[round].data.len, 63U);
            EXPECT_EQ(sent[round].data.data[1], 11 + round);
            EXPECT_EQ(sent[round].data.data[10], round == 0 ? 21 : 0);
            EXPECT_EQ(sent[round].data.data[55], 71 + round);
            EXPECT_EQ(sent[round].data.data[18], 0U);
        }
    }
}

TEST_F(SubMotorSendTest, InvalidSlotRejectedBeforeQueueAndOtherBusNotFlushed) {
    auto other = adapter->GetBus(1);
    auto first = BaseAdapterTestAccess::MakeDeviceWriter(adapter, bus);
    auto second = BaseAdapterTestAccess::MakeDeviceWriter(adapter, other);
    bus->SetSyncMode(true);
    other->SetSyncMode(true);
    EXPECT_THROW(first(Slot(100, 1, 11, 9)), std::invalid_argument);
    auto rtr = Slot(100, 1, 11);
    rtr.frame_flags = kCanFrameFlagRtr;
    EXPECT_THROW(first(rtr), std::invalid_argument);
    auto invalid_flags = Slot(100, 1, 11);
    invalid_flags.frame_flags = 0x80;
    EXPECT_THROW(first(invalid_flags), std::invalid_argument);
    first(Slot(100, 1, 11));
    second(Slot(100, 1, 21));
    bus->Commit();
    ASSERT_EQ(adapter->GetSentMessages().size(), 1U);
    EXPECT_EQ(adapter->GetSentMessages()[0].data.data[1], 11U);
    other->Commit();
    ASSERT_EQ(adapter->GetSentMessages().size(), 2U);
    EXPECT_EQ(adapter->GetSentMessages()[1].bus_idx, 1);
    EXPECT_EQ(adapter->GetSentMessages()[1].data.data[1], 21U);
}

TEST_F(SubMotorSendTest, ExplicitAndPackedCreationShareDeviceAndOrdinaryMessagesStayUnchanged) {
    const auto id = MakeCanAddress(0x12345, 7);
    auto* first = bus->GetMotor(0x12345U, 7U, MotorModel::EC_A4310_P2);
    EXPECT_EQ(first, bus->GetMotor(id, MotorModel::EC_A4310_P2));
    EXPECT_EQ(first, bus->GetMotor(LegacyCanIdBits(id), MotorModel::EC_A4310_P2));
    EXPECT_EQ(first, bus->SelectMotor(id));
    EXPECT_THROW(bus->GetMotor(100U, 0U, MotorModel::EC_A4310_P2), std::invalid_argument);
    EXPECT_THROW(bus->GetMotor(100U, 8U, MotorModel::EC_A4310_P2), std::invalid_argument);
    adapter->ClearSentMessages();
    auto writer = BaseAdapterTestAccess::MakeDeviceWriter(adapter, bus);
    const auto original = Slot(100, 0, 9, 4);
    writer(original);
    ASSERT_EQ(adapter->GetSentMessages().size(), 1U);
    const auto result = adapter->GetSentMessages()[0].data;
    EXPECT_EQ(result.id, original.id);
    EXPECT_EQ(result.len, original.len);
    EXPECT_EQ(result.frame_flags, original.frame_flags);
    EXPECT_EQ(std::memcmp(result.data, original.data, 64), 0);
}

TEST_F(SubMotorSendTest, ParameterQueryBypassesSyncAndReceivesItsOwnPaddedSlot) {
    bus->SetSyncMode(true);
    auto* motor = bus->GetMotor(100U, 3U, MotorModel::EC_A4310_P2);
    adapter->ClearSentMessages();
    auto result = std::async(std::launch::async, [motor] {
        return motor->GetParameter<MotorParameter::CanTimeout>();
    });
    ASSERT_TRUE(adapter->WaitForSentCount(1));
    const auto query = adapter->GetSentMessages()[0];
    EXPECT_EQ(query.data.id, 100U);
    EXPECT_EQ(query.data.len, 54U);
    EXPECT_EQ(query.data.data[19], static_cast<uint8_t>(7 << 5));
    EXPECT_EQ(query.data.data[20], static_cast<uint8_t>(MotorParameter::CanTimeout));
    MotorMessage reply{};
    reply.bus_idx = bus->GetBusIndex();
    reply.data.id = 100;
    reply.data.frame_flags = kCanFrameFlagFdMask;
    reply.data.len = 64;
    reply.data.data[18] = 4;
    reply.data.data[19] = 5 << 5;
    reply.data.data[20] = static_cast<uint8_t>(MotorParameter::CanTimeout);
    reply.data.data[22] = 42;
    adapter->SimulateOnMessage({reply});
    EXPECT_EQ(result.get(), 42);
    adapter->ClearSentMessages();
    motor->SpdControl<0>(1.0F, 2.0F);
    EXPECT_TRUE(adapter->GetSentMessages().empty());
    bus->Commit();
    ASSERT_EQ(adapter->GetSentMessages().size(), 1U);
    EXPECT_EQ(adapter->GetSentMessages()[0].data.len, 54U);
    EXPECT_FALSE(motor->SetId(12, false));
    EXPECT_FALSE(bus->ResetMotorsId(false));
    EXPECT_FALSE(bus->StartAutoAssignCanId(1));
    EXPECT_EQ(adapter->GetSentMessages().size(), 1U);
}

TEST(SubMotorPacketTest, SixSlotsCarryIndependentDlcAndZeroFillUnusedBytes) {
    MotorMessages source;
    for (unsigned sub = 1; sub <= 6; ++sub) {
        source.push_back({0, Slot(100, sub, 0xAA, static_cast<uint8_t>(sub))});
    }
    const auto packets = PackSubMotorMessages(source, true);
    ASSERT_EQ(packets.size(), 1U);
    EXPECT_EQ(packets[0].data.len, 54U);
    for (unsigned sub = 1; sub <= 6; ++sub) {
        const auto offset = (sub - 1) * 9;
        EXPECT_EQ(packets[0].data.data[offset], sub);
        for (unsigned byte = 1; byte <= 8; ++byte) {
            EXPECT_EQ(packets[0].data.data[offset + byte], byte <= sub ? 0xAA : 0);
        }
    }
    for (unsigned byte = 54; byte < 64; ++byte) {
        EXPECT_EQ(packets[0].data.data[byte], 0);
    }
    const auto empty = PackSubMotorMessages({{0, Slot(100, 7, 0, 0)}}, false);
    EXPECT_EQ(empty[0].data.len, 63);
    EXPECT_EQ(empty[0].data.data[54], 0);
}

TEST(SubMotorPacketTest, GroupsDoNotCrossIdsFlagsOrOrdinaryRecordBarriers) {
    MotorMessages source{
        {0, Slot(1, 1, 10)}, {0, Slot(2, 2, 20)}, {0, Slot(1, 0, 30)}, {0, Slot(1, 2, 40)}};
    const auto packets = PackSubMotorMessages(source, true);
    ASSERT_EQ(packets.size(), 4U);
    EXPECT_EQ(packets[0].data.data[9], 0);
    EXPECT_EQ(packets[2].data.len, 8);
    EXPECT_EQ(packets[3].data.data[0], 0);
    source = {{0, Slot(1, 1, 10)}, {0, Slot(1, 2, 20)}};
    source[1].data.frame_flags = kCanFrameFlagEff;
    EXPECT_EQ(PackSubMotorMessages(source, true).size(), 2U);
    EXPECT_EQ(PackSubMotorMessages(source, false).size(), 2U);
    EXPECT_EQ(MakeCanAddress(kPhysicalCanIdMask, 7), 0xFFFFFFFFU);
    EXPECT_EQ(static_cast<CanId>(LegacyCanIdBits(0xFFFFFFFFU)), 0xFFFFFFFFU);
    EXPECT_THROW(MakeCanAddress(0x20000000, 1), std::invalid_argument);
}

}  // namespace
}  // namespace encos
