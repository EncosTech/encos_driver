#include <thread>

#include "battery/battery.h"
#include "device_status_test_access.h"
#include "imu/imu.h"
#include "test_fixtures.h"

namespace encos {
namespace {

MotorMessage MakeImuAcceleration(uint16_t index, uint8_t x_low = 0x00) {
    MotorMessage message{};
    message.bus_idx = 0;
    message.data.id = 0x0CF02D59u + index;
    message.data.frame_flags = kCanFrameFlagEff;
    message.data.len = 6;
    message.data.data[0] = x_low;
    message.data.data[1] = 0x7D;
    message.data.data[2] = 0x64;
    message.data.data[3] = 0x7D;
    message.data.data[4] = 0x9C;
    message.data.data[5] = 0x7C;
    return message;
}

TEST_F(MotorTestFixture, ImuCallbackRunsOnReceiveThreadWithoutPolling) {
    auto* imu = bus->GetImu(0);
    ASSERT_NE(imu, nullptr);

    int callback_count = 0;
    std::thread::id callback_thread;
    imu->SetOnStatus([&](const ImuStatus& status) {
        ++callback_count;
        callback_thread = std::this_thread::get_id();
        ASSERT_TRUE(status.acceleration.has_value());
        EXPECT_NEAR(status.acceleration->x, 0.0f, 1e-5f);
    });

    const auto receive_thread = std::this_thread::get_id();
    adapter->InjectMessage(MakeImuAcceleration(0));

    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(callback_thread, receive_thread);
    ASSERT_TRUE(imu->GetStatus().acceleration.has_value());
}

TEST_F(MotorTestFixture, ImuDirectRouteUpdatesAllGroupsAndAllowsCallbackReentry) {
    auto* imu = bus->GetImu(3);
    ASSERT_NE(imu, nullptr);

    int callback_count = 0;
    imu->SetOnStatus([&](const ImuStatus&) {
        ++callback_count;
        EXPECT_TRUE(imu->GetStatus().acceleration.has_value());
    });

    adapter->InjectMessage(MakeImuAcceleration(3));
    MotorMessage angular{};
    angular.bus_idx = 0;
    angular.data.id = 0x0CF02A5Cu;
    angular.data.frame_flags = kCanFrameFlagEff;
    angular.data.len = 8;
    adapter->InjectMessage(angular);
    MotorMessage euler{};
    euler.bus_idx = 0;
    euler.data.id = 0x0CF0295Cu;
    euler.data.frame_flags = kCanFrameFlagEff;
    euler.data.len = 6;
    adapter->InjectMessage(euler);
    MotorMessage quaternion{};
    quaternion.bus_idx = 0;
    quaternion.data.id = 0x0CF0305Cu;
    quaternion.data.frame_flags = kCanFrameFlagEff;
    quaternion.data.len = 8;
    adapter->InjectMessage(quaternion);

    const auto status = imu->GetStatus();
    EXPECT_EQ(callback_count, 4);
    EXPECT_TRUE(status.acceleration.has_value());
    EXPECT_TRUE(status.angular_velocity.has_value());
    EXPECT_TRUE(status.euler_angle.has_value());
    EXPECT_TRUE(status.quaternion.has_value());
}

TEST_F(MotorTestFixture, ImuDirectRouteRejectsMalformedFramesAndIsolatesIndices) {
    auto* first = bus->GetImu(1);
    auto* second = bus->GetImu(2);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    int first_callbacks = 0;
    int second_callbacks = 0;
    first->SetOnStatus([&](const ImuStatus&) {
        ++first_callbacks;
    });
    second->SetOnStatus([&](const ImuStatus&) {
        ++second_callbacks;
    });

    auto malformed = MakeImuAcceleration(1);
    malformed.data.len = 5;
    adapter->InjectMessage(malformed);
    EXPECT_EQ(first_callbacks, 0);
    EXPECT_FALSE(first->GetStatus().acceleration.has_value());

    adapter->InjectMessage(MakeImuAcceleration(1));
    adapter->InjectMessage(MakeImuAcceleration(2));
    EXPECT_EQ(first_callbacks, 1);
    EXPECT_EQ(second_callbacks, 1);
}

TEST_F(MotorTestFixture, ImuStatusGroupsExpireIndependently) {
    auto* imu = bus->GetImu(5);
    ASSERT_NE(imu, nullptr);

    adapter->InjectMessage(MakeImuAcceleration(5));
    MotorMessage angular{};
    angular.bus_idx = 0;
    angular.data.id = 0x0CF02A5Eu;
    angular.data.frame_flags = kCanFrameFlagEff;
    angular.data.len = 8;
    adapter->InjectMessage(angular);
    DeviceStatusTestAccess::ExpireImuAcceleration(imu);

    const auto status = imu->GetStatus();
    EXPECT_FALSE(status.acceleration.has_value());
    EXPECT_TRUE(status.angular_velocity.has_value());
}

TEST_F(MotorTestFixture, ImuDeletionUnregistersDirectReportRoutes) {
    auto& manager = EncosDriverManager::Instance();
    auto* imu = bus->GetImu(4);
    auto* remaining = bus->GetBattery(4);
    ASSERT_NE(imu, nullptr);
    ASSERT_NE(remaining, nullptr);
    ASSERT_TRUE(manager.DestroyImu(imu));

    EXPECT_FALSE(manager.DispatchReceive(adapter, 0, MakeImuAcceleration(4).data));
    for (const auto id : {0x0CF02A5Du, 0x0CF0295Du, 0x0CF0305Du}) {
        MotorPackMsg message{};
        message.id = id;
        message.frame_flags = kCanFrameFlagEff;
        message.len = 8;
        EXPECT_FALSE(manager.DispatchReceive(adapter, 0, message));
    }

    MotorPackMsg battery_message{};
    battery_message.id = 0x3F8u;
    battery_message.len = 8;
    battery_message.data[1] = 60;
    EXPECT_TRUE(manager.DispatchReceive(adapter, 0, battery_message));
    ASSERT_TRUE(remaining->GetStatus().state.has_value());
    EXPECT_FLOAT_EQ(remaining->GetStatus().state->soc, 0.60f);
}

}  // namespace
}  // namespace encos
