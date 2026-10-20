// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "test_claim_device.h"

#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>

#include "device/device.h"
#include "encos/driver_manager.h"
#include "encos/encos_driver.h"
#include "external/json.hpp"
#include "test_fixtures.h"

namespace encos {
namespace {

MotorMessage MakeExternalFrame(std::uint32_t id) {
    MotorMessage message{};
    message.bus_idx = 0;
    message.data.id = id;
    message.data.len = 3;
    message.data.data[0] = 0x11;
    message.data.data[1] = 0x22;
    message.data.data[2] = 0x33;
    return message;
}

TestClaimDevice* ClaimDevice(Bus* bus) {
    Device* device = bus->GetDevice("TestClaimDevice");
    return dynamic_cast<TestClaimDevice*>(device);
}

TEST_F(MotorTestFixture, ClaimDeviceReceivesUnmatchedFrames) {
    auto* claim = ClaimDevice(bus);
    ASSERT_NE(claim, nullptr);

    adapter->InjectMessage(MakeExternalFrame(0x123u));

    EXPECT_EQ(claim->UpdateCount(), 1);
    ASSERT_TRUE(claim->LastMessage().has_value());
    EXPECT_EQ(claim->LastMessage()->id, 0x123u);

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyDevice(claim));
}

TEST_F(MotorTestFixture, PreciseRouteWinsOverClaimDevice) {
    auto* claim = ClaimDevice(bus);
    ASSERT_NE(claim, nullptr);

    // 电机（id 1）拥有精确路由，认领设备不应收到该帧。
    adapter->InjectMessage(MakeExternalFrame(1u));
    EXPECT_EQ(claim->UpdateCount(), 0);

    // 无精确路由的帧交给认领设备。
    adapter->InjectMessage(MakeExternalFrame(0x321u));
    EXPECT_EQ(claim->UpdateCount(), 1);

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyDevice(claim));
}

TEST_F(MotorTestFixture, ClaimDeviceReceivesSystemFrames) {
    auto* claim = ClaimDevice(bus);
    ASSERT_NE(claim, nullptr);

    adapter->InjectMessage(MakeExternalFrame(0x7FFu));

    EXPECT_EQ(claim->UpdateCount(), 1);
    ASSERT_TRUE(claim->LastMessage().has_value());
    EXPECT_EQ(claim->LastMessage()->id, 0x7FFu);

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyDevice(claim));
}

TEST_F(MotorTestFixture, SecondClaimOnSameBusIsRejected) {
    auto* claim = ClaimDevice(bus);
    ASSERT_NE(claim, nullptr);

    // 不同配置即不同实例，同一总线已存在认领项，创建必须失败回滚。
    EXPECT_THROW(bus->GetDevice("TestClaimDevice", nlohmann::json{{"variant", 1u}}),
                 std::runtime_error);

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyDevice(claim));
}

TEST_F(MotorTestFixture, DestroyClaimReleasesBusSlot) {
    auto* claim = ClaimDevice(bus);
    ASSERT_NE(claim, nullptr);
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyDevice(claim));

    // 认领槽已随销毁释放，可重新认领同一总线。
    auto* recreated = ClaimDevice(bus);
    ASSERT_NE(recreated, nullptr);
    adapter->InjectMessage(MakeExternalFrame(0x456u));
    EXPECT_EQ(recreated->UpdateCount(), 1);

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyDevice(recreated));
}

TEST_F(MotorTestFixture, AvailableDeviceTypesIncludesTestClaimDevice) {
    const auto types = GetAvailableDeviceTypes();
    EXPECT_NE(std::find(types.begin(), types.end(), "TestClaimDevice"), types.end());
}

}  // namespace
}  // namespace encos
