#include <thread>

#include "battery/battery.h"
#include "device_status_test_access.h"
#include "pms/pms.h"
#include "test_fixtures.h"

namespace encos {
namespace {

MotorMessage MakePmsFrame(uint32_t id) {
    MotorMessage message{};
    message.bus_idx = 0;
    message.data.id = id;
    message.data.frame_flags = kCanFrameFlagEff;
    message.data.len = 8;
    message.data.data[0] = 0x01;
    message.data.data[1] = 50;
    return message;
}

TEST_F(MotorTestFixture, PmsCallbackCoalescesFramesOnReceiveThread) {
    auto* pms = bus->GetPms();
    ASSERT_NE(pms, nullptr);

    int callback_count = 0;
    std::thread::id callback_thread;
    pms->SetOnStatus([&](const PmsStatus& status) {
        ++callback_count;
        callback_thread = std::this_thread::get_id();
        EXPECT_EQ(status.battery_soc, 50);
    });

    adapter->InjectMessage(MakePmsFrame(0x18F0FFF2));
    adapter->InjectMessage(MakePmsFrame(0x18F1FFF2));
    EXPECT_EQ(callback_count, 0);

    const auto receive_thread = std::this_thread::get_id();
    adapter->InjectMessage(MakePmsFrame(0x18F2FFF2));

    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(callback_thread, receive_thread);
    EXPECT_TRUE(pms->GetStatus().has_value());
}

TEST_F(MotorTestFixture, PmsDirectRouteRejectsMalformedFramesAndAllowsCallbackReentry) {
    auto* pms = bus->GetPms();
    ASSERT_NE(pms, nullptr);

    int callback_count = 0;
    pms->SetOnStatus([&](const PmsStatus&) {
        ++callback_count;
        EXPECT_TRUE(pms->GetStatus().has_value());
    });

    auto malformed = MakePmsFrame(0x18F0FFF2);
    malformed.data.len = 7;
    adapter->InjectMessage(malformed);
    EXPECT_EQ(callback_count, 0);
    EXPECT_FALSE(pms->GetStatus().has_value());

    adapter->InjectMessage(MakePmsFrame(0x18F0FFF2));
    adapter->InjectMessage(MakePmsFrame(0x18F1FFF2));
    EXPECT_EQ(callback_count, 0);
    adapter->InjectMessage(MakePmsFrame(0x18F2FFF2));
    EXPECT_EQ(callback_count, 1);
}

TEST_F(MotorTestFixture, PmsCompleteStatusExpiresWithoutTimeoutCallback) {
    auto* pms = bus->GetPms();
    ASSERT_NE(pms, nullptr);

    int callback_count = 0;
    pms->SetOnStatus([&](const PmsStatus&) {
        ++callback_count;
    });
    adapter->InjectMessage(MakePmsFrame(0x18F0FFF2));
    adapter->InjectMessage(MakePmsFrame(0x18F1FFF2));
    adapter->InjectMessage(MakePmsFrame(0x18F2FFF2));
    ASSERT_EQ(callback_count, 1);

    DeviceStatusTestAccess::ExpireAllPmsFrames(pms);
    EXPECT_FALSE(pms->GetStatus().has_value());
    EXPECT_EQ(callback_count, 1);

    adapter->InjectMessage(MakePmsFrame(0x18F0FFF2));
    adapter->InjectMessage(MakePmsFrame(0x18F1FFF2));
    EXPECT_FALSE(pms->GetStatus().has_value());
    EXPECT_EQ(callback_count, 1);
}

TEST_F(MotorTestFixture, PmsDeletionUnregistersDirectReportRoutes) {
    auto& manager = EncosDriverManager::Instance();
    auto* pms = bus->GetPms();
    auto* remaining = bus->GetBattery(9);
    ASSERT_NE(pms, nullptr);
    ASSERT_NE(remaining, nullptr);
    ASSERT_TRUE(manager.DestroyPms(pms));

    for (const auto id : {0x18F0FFF2u, 0x18F1FFF2u, 0x18F2FFF2u}) {
        EXPECT_FALSE(manager.DispatchReceive(adapter, 0, MakePmsFrame(id).data));
    }

    MotorPackMsg battery_message{};
    battery_message.id = 0x3FDu;
    battery_message.len = 8;
    battery_message.data[1] = 70;
    EXPECT_TRUE(manager.DispatchReceive(adapter, 0, battery_message));
    ASSERT_TRUE(remaining->GetStatus().state.has_value());
    EXPECT_FLOAT_EQ(remaining->GetStatus().state->soc, 0.70f);
}

}  // namespace
}  // namespace encos
