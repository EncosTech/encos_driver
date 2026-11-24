// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <initializer_list>
#include <stdexcept>

#include "driver_manager_test_access.h"
#include "encos/encos_driver.h"
#include "test_adapter.h"

namespace encos {
namespace {

using namespace std::chrono_literals;

class DexHandTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& manager = EncosDriverManager::Instance();
        DriverManagerTestAccess::Reset(manager);
        adapter_ = static_cast<TestAdapter*>(manager.CreateAdapterWithFactory("dex-hand", [] {
            return new TestAdapter("dex-hand");
        }));
        bus_ = adapter_->GetBus(6);
    }

    void TearDown() override {
        DriverManagerTestAccess::Reset(EncosDriverManager::Instance());
    }

    void Reply(int bus_idx, CanId id, uint8_t flags, std::initializer_list<uint8_t> bytes) {
        MotorMessage message{};
        message.bus_idx = bus_idx;
        message.data.id = id;
        message.data.frame_flags = flags;
        message.data.len = static_cast<uint8_t>(bytes.size());
        std::copy(bytes.begin(), bytes.end(), message.data.data);
        adapter_->SimulateOnMessage({message});
    }

    void ExpectRequest(std::size_t index, CanId id, uint8_t flags,
                       std::initializer_list<uint8_t> bytes) {
        const auto sent = adapter_->GetSentMessages();
        ASSERT_LT(index, sent.size());
        EXPECT_EQ(sent[index].bus_idx, 6);
        const auto& message = sent[index].data;
        EXPECT_EQ(message.id, id);
        EXPECT_EQ(message.frame_flags, flags);
        ASSERT_EQ(message.len, bytes.size());
        EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), message.data));
    }

    TestAdapter* adapter_ = nullptr;
    Bus* bus_ = nullptr;
};

TEST_F(DexHandTest, SamplePointRequestsUseExactLengthsAndLittleEndianValues) {
    auto* hand = bus_->GetDexHand();
    EXPECT_EQ(hand, bus_->GetDexHand(1));
    EXPECT_FALSE(bus_->HasExternalDevice());
    bus_->SetSyncMode(true);

    auto query = std::async(std::launch::async, [hand] {
        return hand->GetSamplePoints({true, 1s});
    });
    ASSERT_TRUE(adapter_->WaitForSentCount(1));
    ExpectRequest(0, 1, kCanFrameFlagFdMask, {0xF0, 0x00});
    Reply(6, 1, kCanFrameFlagFdMask, {0xF4, 0x00, 0x00, 0xEE, 0x02, 0x6B, 0x03});
    auto reply = query.get();
    ASSERT_EQ(reply.status, DexHandStatus::Success);
    ASSERT_TRUE(reply.value.has_value());
    EXPECT_EQ(reply.value->nominal, 750);
    EXPECT_EQ(reply.value->data, 875);

    auto configure = std::async(std::launch::async, [hand] {
        return hand->SetSamplePoints({750, 875}, {false, 1s});
    });
    ASSERT_TRUE(adapter_->WaitForSentCount(2));
    ExpectRequest(1, 1, 0, {0xF8, 0x00, 0xEE, 0x02, 0x6B, 0x03});
    Reply(6, 1, 0, {0xFC, 0x00, 0x00, 0xED, 0x02, 0x6B, 0x03});
    Reply(6, 1, 0, {0xFC, 0x00, 0x00, 0xEE, 0x02, 0x6B, 0x03});
    reply = configure.get();
    ASSERT_EQ(reply.status, DexHandStatus::Success);
    ASSERT_TRUE(reply.value.has_value());
    EXPECT_EQ(reply.value->nominal, 750);
    EXPECT_EQ(reply.value->data, 875);
    EXPECT_TRUE(adapter_->GetSynchronizedBatches().empty());
}

TEST_F(DexHandTest, BaseIdCanBeQueriedViaBroadcastAndChangedViaOldId) {
    auto* broadcast = bus_->GetDexHand(0);
    auto query = std::async(std::launch::async, [broadcast] {
        return broadcast->GetBaseId({false, 1s});
    });
    ASSERT_TRUE(adapter_->WaitForSentCount(1));
    ExpectRequest(0, 0, 0, {0xF0, 0x01});
    Reply(6, 0, 0, {0xF4, 0x01, 0x00, 0x12});
    EXPECT_EQ(query.get().value, 0x12);

    auto* hand = bus_->GetDexHand(0x12);
    auto* next = bus_->GetDexHand(0x15);
    auto configure = std::async(std::launch::async, [hand] {
        return hand->SetBaseId(0x15, {false, 1s});
    });
    ASSERT_TRUE(adapter_->WaitForSentCount(2));
    ExpectRequest(1, 0x12, 0, {0xF8, 0x01, 0x15});
    Reply(6, 0x15, 0, {0xFC, 0x01, 0x00, 0x15});
    Reply(6, 0x12, 0, {0xFC, 0x01, 0x00, 0x16});
    EXPECT_EQ(configure.wait_for(0ms), std::future_status::timeout);
    Reply(6, 0x12, 0, {0xFC, 0x01, 0x00, 0x15});
    EXPECT_EQ(configure.get().value, 0x15);

    query = std::async(std::launch::async, [next] {
        return next->GetBaseId({false, 1s});
    });
    ASSERT_TRUE(adapter_->WaitForSentCount(3));
    ExpectRequest(2, 0x15, 0, {0xF0, 0x01});
    Reply(6, 0x15, 0, {0xF4, 0x01, 0x00, 0x15});
    EXPECT_EQ(query.get().value, 0x15);
}

TEST_F(DexHandTest, RejectsUnrelatedFramesAndReportsFirmwareFailures) {
    auto* hand = bus_->GetDexHand();
    for (const auto status : {DexHandStatus::InvalidParameter, DexHandStatus::SaveFailed,
                              DexHandStatus::FirmwareBusy}) {
        const auto status_byte = static_cast<uint8_t>(status);
        auto result = std::async(std::launch::async, [hand] {
            return hand->SetBaseId(2, {false, 1s});
        });
        ASSERT_TRUE(adapter_->WaitForSentCount(status_byte));
        Reply(5, 1, 0, {0xFC, 0x01, 0x00, 2});
        Reply(6, 1, kCanFrameFlagEff, {0xFC, 0x01, 0x00, 2});
        Reply(6, 1, kCanFrameFlagRtr, {0xFC, 0x01, 0x00, 2});
        Reply(6, 1, kCanFrameFlagFdMask, {0xFC, 0x01, 0x00, 2});
        Reply(6, 1, 0, {0xF4, 0x01, 0x00, 2});
        Reply(6, 1, 0, {0xFC, 0x00, 0x00, 2});
        Reply(6, 1, 0, {0xFC, 0x01, status_byte, 2});
        Reply(6, 1, 0, {0xFC, 0x01, 0x00});
        Reply(6, 1, 0, {0xFC, 0x01, 0x04});
        Reply(6, 1, 0, {0xFC, 0x01, status_byte});
        const auto reply = result.get();
        EXPECT_EQ(reply.status, status);
        EXPECT_FALSE(reply.value.has_value());
    }
}

TEST_F(DexHandTest, ValidatesIdsAndTimesOutWithoutAReply) {
    EXPECT_THROW(bus_->GetDexHand(-1), std::invalid_argument);
    EXPECT_THROW(bus_->GetDexHand(248), std::invalid_argument);
    auto* hand = bus_->GetDexHand(247);
    EXPECT_THROW(hand->SetBaseId(0), std::invalid_argument);
    EXPECT_THROW(hand->SetBaseId(248), std::invalid_argument);
    EXPECT_TRUE(adapter_->GetSentMessages().empty());
    const auto reply = hand->GetBaseId({false, 10ms});
    EXPECT_EQ(reply.status, DexHandStatus::Timeout);
    EXPECT_FALSE(reply.value.has_value());
    ExpectRequest(0, 247, 0, {0xF0, 0x01});
}

TEST_F(DexHandTest, CanRecreateTheManagementDeviceAfterDeletion) {
    ASSERT_TRUE(DeleteDexHand(bus_->GetDexHand()));
    auto* hand = bus_->GetDexHand();
    auto result = std::async(std::launch::async, [hand] {
        return hand->GetBaseId({false, 1s});
    });
    ASSERT_TRUE(adapter_->WaitForSentCount(1));
    Reply(6, 1, 0, {0xF4, 0x01, 0x00, 1});
    EXPECT_EQ(result.get().value, 1);
}

TEST_F(DexHandTest, AllTwentyKnownModelJointsUseLocalRangesWithoutAQuery) {
    auto* hand = bus_->GetDexHand();
    ASSERT_NE(hand, nullptr);
    const auto model = MotorModel::EC_DEXTEROUS_HAND;
    const auto expected = GetMotorModelRanges(model);
    for (unsigned joint = 0; joint < 20; ++joint) {
        const auto can_id = 1U + joint / 7U;
        const auto sub_id = 1U + joint % 7U;
        auto* motor = bus_->GetMotor(can_id, sub_id, model);
        ASSERT_NE(motor, nullptr);
        EXPECT_EQ(bus_->SelectMotor(MakeCanAddress(can_id, sub_id)), motor);
        EXPECT_TRUE(motor->IsCanFdEnabled());
        const auto properties = motor->GetProperties();
        EXPECT_EQ(properties.position.min, expected.position.min);
        EXPECT_EQ(properties.position.max, expected.position.max);
        EXPECT_EQ(properties.fw_version, 0U);
    }
    EXPECT_TRUE(adapter_->GetSentMessages().empty());
}

TEST_F(DexHandTest, ConcurrentRequestIsRejectedAndDeletingBusCancelsWaiter) {
    auto* hand = bus_->GetDexHand();
    auto result = std::async(std::launch::async, [hand] {
        return hand->GetBaseId({false, 2s});
    });
    ASSERT_TRUE(adapter_->WaitForSentCount(1));
    const auto concurrent = hand->GetSamplePoints();
    EXPECT_EQ(concurrent.status, DexHandStatus::RequestInProgress);
    EXPECT_EQ(adapter_->GetSentMessages().size(), 1U);
    ASSERT_TRUE(DeleteBus(bus_));
    const auto reply = result.get();
    EXPECT_EQ(reply.status, DexHandStatus::Canceled);
    EXPECT_FALSE(reply.value.has_value());
}

TEST_F(DexHandTest, ManagementAndSubMotorRepliesShareThePhysicalAddress) {
    auto* hand = bus_->GetDexHand();
    auto* motor = bus_->GetMotor(1U, 3U, MotorModel::EC_DEXTEROUS_HAND);
    adapter_->ClearSentMessages();
    auto motor_result = std::async(std::launch::async, [motor] {
        return motor->GetParameter<MotorParameter::CanTimeout>();
    });
    ASSERT_TRUE(adapter_->WaitForSentCount(1));
    auto hand_result = std::async(std::launch::async, [hand] {
        return hand->GetBaseId({true, 1s});
    });
    ASSERT_TRUE(adapter_->WaitForSentCount(2));

    MotorMessage grouped{};
    grouped.bus_idx = 6;
    grouped.data.id = 1;
    grouped.data.frame_flags = kCanFrameFlagFdMask;
    grouped.data.len = 64;
    grouped.data.data[18] = 4;
    grouped.data.data[19] = 0xA0;
    grouped.data.data[20] = static_cast<uint8_t>(MotorParameter::CanTimeout);
    grouped.data.data[22] = 42;
    adapter_->SimulateOnMessage({grouped});
    Reply(6, 1, kCanFrameFlagFdMask, {0xF4, 0x01, 0x00, 1});
    EXPECT_EQ(motor_result.get(), 42);
    EXPECT_EQ(hand_result.get().value, 1);
}

}  // namespace
}  // namespace encos
