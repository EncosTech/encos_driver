#include <thread>

#include "battery/battery.h"
#include "device_status_test_access.h"
#include "imu/imu.h"
#include "test_fixtures.h"

namespace encos {
namespace {

MotorMessage MakeBatteryState(uint16_t index, uint8_t soc) {
    MotorMessage message{};
    message.bus_idx = 0;
    message.data.id = 0x3F4u + index;
    message.data.len = 8;
    message.data.data[0] = 1;
    message.data.data[1] = soc;
    message.data.data[2] = 0x5E;
    message.data.data[3] = 0x01;
    message.data.data[4] = 0xD0;
    message.data.data[5] = 0x07;
    message.data.data[6] = 0xB8;
    message.data.data[7] = 0x0B;
    return message;
}

MotorMessage MakeBatteryTemp(uint16_t index) {
    MotorMessage message{};
    message.bus_idx = 0;
    message.data.id = 0x2F4u + index;
    message.data.len = 8;
    message.data.data[0] = 0x19;
    message.data.data[1] = 0x00;
    message.data.data[2] = 0x1E;
    message.data.data[3] = 0x00;
    message.data.data[4] = 0xE8;
    message.data.data[5] = 0x03;
    message.data.data[6] = 0xF4;
    message.data.data[7] = 0x01;
    return message;
}

MotorMessage MakeBatteryError(uint16_t index) {
    MotorMessage message{};
    message.bus_idx = 0;
    message.data.id = 0x0F4u + index;
    message.data.len = 2;
    message.data.data[0] = 0x01;
    message.data.data[1] = 0x04;
    return message;
}

MotorMessage MakeBatteryActiveCommands(uint16_t index) {
    MotorMessage message{};
    message.bus_idx = 0;
    message.data.id = 0x1F4u + index;
    message.data.len = 1;
    message.data.data[0] = 0x3F;
    return message;
}

TEST_F(MotorTestFixture, BatteryCallbackRunsSynchronouslyForEveryValidFrame) {
    auto* battery = bus->GetBattery(2);
    ASSERT_NE(battery, nullptr);

    int callback_count = 0;
    std::thread::id callback_thread;
    battery->SetOnStatus([&](const BatteryStatus& status) {
        ++callback_count;
        callback_thread = std::this_thread::get_id();
        ASSERT_TRUE(status.state.has_value());
        EXPECT_FLOAT_EQ(status.state->soc, 0.55f);
    });

    const auto receive_thread = std::this_thread::get_id();
    adapter->InjectMessage(MakeBatteryState(2, 55));

    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(callback_thread, receive_thread);
    ASSERT_TRUE(battery->GetStatus().state.has_value());
    EXPECT_FLOAT_EQ(battery->GetStatus().state->soc, 0.55f);

    adapter->InjectMessage(MakeBatteryState(2, 55));
    EXPECT_EQ(callback_count, 2);
}

TEST_F(MotorTestFixture, BatteryInitialAllZeroErrorFrameStillInvokesCallback) {
    auto* battery = bus->GetBattery(3);
    ASSERT_NE(battery, nullptr);

    int callback_count = 0;
    battery->SetOnStatus([&](const BatteryStatus&) {
        ++callback_count;
    });

    auto error = MakeBatteryError(3);
    error.data.data[0] = 0;
    error.data.data[1] = 0;
    adapter->InjectMessage(error);

    EXPECT_EQ(callback_count, 1);
}

TEST_F(MotorTestFixture, BatteryDirectRouteDecodesEveryReportIdAndAllowsCallbackReentry) {
    auto* battery = bus->GetBattery(4);
    ASSERT_NE(battery, nullptr);

    int callback_count = 0;
    battery->SetOnStatus([&](const BatteryStatus&) {
        ++callback_count;
        EXPECT_TRUE(battery->GetStatus().state.has_value());
    });

    adapter->InjectMessage(MakeBatteryState(4, 55));
    adapter->InjectMessage(MakeBatteryTemp(4));
    adapter->InjectMessage(MakeBatteryError(4));
    adapter->InjectMessage(MakeBatteryActiveCommands(4));

    const auto status = battery->GetStatus();
    ASSERT_TRUE(status.state.has_value());
    ASSERT_TRUE(status.temp.has_value());
    ASSERT_TRUE(status.active_commands.has_value());
    EXPECT_EQ(callback_count, 4);
    EXPECT_FLOAT_EQ(status.state->soc, 0.55f);
    EXPECT_FLOAT_EQ(status.state->voltage, 3.5f);
    EXPECT_FLOAT_EQ(status.state->allowed_discharge_current, 0.02f);
    EXPECT_FLOAT_EQ(status.state->allowed_charge_current, 0.03f);
    EXPECT_FLOAT_EQ(status.temp->battery, 25.0f);
    EXPECT_FLOAT_EQ(status.temp->mos, 30.0f);
    EXPECT_FLOAT_EQ(status.temp->discharge_current, 0.01f);
    EXPECT_FLOAT_EQ(status.temp->charge_current, 0.005f);
    EXPECT_TRUE(status.error.could_not_charge);
    EXPECT_TRUE(status.error.charger_fault);
    EXPECT_TRUE(status.active_commands->shutdown_request);
    EXPECT_TRUE(status.active_commands->mos_status);
}

TEST_F(MotorTestFixture, BatteryDirectRouteRejectsMalformedFramesAndIsolatesIndices) {
    auto* first = bus->GetBattery(5);
    auto* second = bus->GetBattery(6);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    int first_callbacks = 0;
    int second_callbacks = 0;
    first->SetOnStatus([&](const BatteryStatus&) {
        ++first_callbacks;
    });
    second->SetOnStatus([&](const BatteryStatus&) {
        ++second_callbacks;
    });

    auto malformed = MakeBatteryState(5, 10);
    malformed.data.len = 7;
    adapter->InjectMessage(malformed);
    auto malformed_error = MakeBatteryError(5);
    malformed_error.data.len = 1;
    adapter->InjectMessage(malformed_error);
    EXPECT_EQ(first_callbacks, 0);
    EXPECT_FALSE(first->GetStatus().state.has_value());

    adapter->InjectMessage(MakeBatteryState(5, 10));
    adapter->InjectMessage(MakeBatteryState(6, 90));
    EXPECT_EQ(first_callbacks, 1);
    EXPECT_EQ(second_callbacks, 1);
    ASSERT_TRUE(first->GetStatus().state.has_value());
    ASSERT_TRUE(second->GetStatus().state.has_value());
    EXPECT_FLOAT_EQ(first->GetStatus().state->soc, 0.10f);
    EXPECT_FLOAT_EQ(second->GetStatus().state->soc, 0.90f);
}

TEST_F(MotorTestFixture, BatteryStatusGroupsExpireIndependentlyAndReportCommTimeout) {
    auto* battery = bus->GetBattery(8);
    ASSERT_NE(battery, nullptr);

    adapter->InjectMessage(MakeBatteryState(8, 55));
    adapter->InjectMessage(MakeBatteryError(8));
    DeviceStatusTestAccess::ExpireBatteryStateAndError(battery);
    adapter->InjectMessage(MakeBatteryTemp(8));

    const auto status = battery->GetStatus();
    EXPECT_FALSE(status.state.has_value());
    ASSERT_TRUE(status.temp.has_value());
    EXPECT_FLOAT_EQ(status.temp->battery, 25.0f);
    EXPECT_TRUE(status.error.comm_timeout);
    EXPECT_FALSE(status.error.could_not_charge);
    EXPECT_FALSE(status.error.charger_fault);
}

TEST_F(MotorTestFixture, BatteryDeletionUnregistersDirectReportRoutes) {
    auto& manager = EncosDriverManager::Instance();
    auto* battery = bus->GetBattery(7);
    auto* remaining = bus->GetImu(7);
    ASSERT_NE(battery, nullptr);
    ASSERT_NE(remaining, nullptr);
    ASSERT_TRUE(manager.DestroyBattery(battery));

    EXPECT_FALSE(manager.DispatchReceive(adapter, 0, MakeBatteryState(7, 55).data));
    EXPECT_FALSE(manager.DispatchReceive(adapter, 0, MakeBatteryTemp(7).data));
    EXPECT_FALSE(manager.DispatchReceive(adapter, 0, MakeBatteryError(7).data));
    EXPECT_FALSE(manager.DispatchReceive(adapter, 0, MakeBatteryActiveCommands(7).data));

    MotorPackMsg imu_message{};
    imu_message.id = 0x0CF02D60u;
    imu_message.frame_flags = kCanFrameFlagEff;
    imu_message.len = 6;
    imu_message.data[1] = 0x7D;
    imu_message.data[3] = 0x7D;
    imu_message.data[4] = 0x9C;
    imu_message.data[5] = 0x7C;
    EXPECT_TRUE(manager.DispatchReceive(adapter, 0, imu_message));
    EXPECT_TRUE(remaining->GetStatus().acceleration.has_value());
}

}  // namespace
}  // namespace encos
