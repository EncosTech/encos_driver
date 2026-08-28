#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

#include "battery/battery.h"
#include "bus/bus.h"
#include "driver_manager_test_access.h"
#include "encos/driver_manager.h"
#include "motor/motor.h"
#include "test_adapter.h"

namespace encos {
namespace {

class AdapterRoutingTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& manager = EncosDriverManager::Instance();
        DriverManagerTestAccess::Reset(manager);
        const auto name = "adapter-routing-" + std::to_string(next_id_++);
        adapter_ = static_cast<TestAdapter*>(manager.CreateAdapterWithFactory(name, [name]() {
            return new TestAdapter(name);
        }));
        bus_ = manager.CreateBus(adapter_, 3);
    }

    void TearDown() override {
        DriverManagerTestAccess::Reset(EncosDriverManager::Instance());
    }

    static unsigned next_id_;
    TestAdapter* adapter_ = nullptr;
    Bus* bus_ = nullptr;
};

unsigned AdapterRoutingTest::next_id_ = 0;

MotorPackMsg MakeStatusMessage(std::uint32_t id) {
    MotorPackMsg message{};
    message.id = id;
    message.len = 8;
    message.data[0] = 1u << 5u;
    message.data[6] = 50;
    message.data[7] = 50;
    return message;
}

TEST_F(AdapterRoutingTest, SerializesRegisteredCallbackDelivery) {
    auto& manager = EncosDriverManager::Instance();
    auto* battery = manager.CreateBattery(bus_, 10);
    ASSERT_NE(battery, nullptr);
    const std::vector<std::uint32_t> ids{0x3FE, 0x2FE, 0x0FE, 0x1FE};

    std::atomic<unsigned> deliveries{0};
    std::atomic<unsigned> active_callbacks{0};
    std::atomic<unsigned> maximum_active_callbacks{0};
    battery->SetOnStatus([&](const BatteryStatus&) {
        const auto active = active_callbacks.fetch_add(1, std::memory_order_acq_rel) + 1;
        maximum_active_callbacks.store(
            std::max(maximum_active_callbacks.load(std::memory_order_acquire), active),
            std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        active_callbacks.fetch_sub(1, std::memory_order_acq_rel);
        deliveries.fetch_add(1, std::memory_order_release);
    });

    MotorMessage message{};
    message.bus_idx = 3;
    message.data.id = ids.front();
    message.data.len = 8;
    message.data.data[1] = 1;
    MotorMessage second_message = message;
    second_message.data.data[1] = 2;
    std::thread first([&]() {
        adapter_->SimulateOnMessage({message});
    });
    std::thread second([&]() {
        adapter_->SimulateOnMessage({second_message});
    });
    first.join();
    second.join();

    EXPECT_EQ(deliveries.load(std::memory_order_acquire), 2u);
    EXPECT_EQ(maximum_active_callbacks.load(std::memory_order_acquire), 1u);
}

TEST_F(AdapterRoutingTest, DropsUnknownBusWithoutCreatingLegacyTraffic) {
    MotorMessage message{};
    message.bus_idx = 0x7FFF;
    message.data.id = 0x426;
    message.data.len = 1;
    adapter_->SimulateOnMessage({message});
}

TEST_F(AdapterRoutingTest, DeliversUnregisteredFramesOutsideLegacyBuffers) {
    MotorMessage message{};
    message.bus_idx = 3;
    message.data.id = 0x426;
    message.data.len = 1;
    adapter_->SimulateOnMessage({message});
}

TEST_F(AdapterRoutingTest, RawCallbackPreservesMixedBatchAndRoutingContinues) {
    auto& manager = EncosDriverManager::Instance();
    auto* battery = manager.CreateBattery(bus_, 0);
    ASSERT_NE(battery, nullptr);
    const std::vector<std::uint32_t> ids{0x3F4, 0x2F4, 0x0F4, 0x1F4};
    std::atomic<unsigned> deliveries{0};
    uint8_t received_flags = 0;
    battery->SetOnStatus([&](const BatteryStatus&) {
        deliveries.fetch_add(1, std::memory_order_relaxed);
    });
    EXPECT_FALSE(DriverManagerTestAccess::RegisterReceiveRoutes(
        manager, battery, adapter_, bus_, ids, [&](const MotorPackMsg& message) {
            received_flags = message.frame_flags;
            deliveries.fetch_add(1, std::memory_order_relaxed);
        }));

    MotorMessages raw_messages;
    adapter_->SetRawMessageCallbackForTests([&](const MotorMessages& messages) {
        raw_messages = messages;
    });
    const uint8_t flags = kCanFrameFlagEff | kCanFrameFlagFdMask | kCanFrameFlagRtr;
    MotorMessage registered{};
    registered.bus_idx = 3;
    registered.data.id = ids.front();
    registered.data.frame_flags = flags;
    registered.data.len = 8;
    registered.data.data[0] = 0xA5;
    registered.data.data[1] = 1;
    MotorMessage unknown{};
    unknown.bus_idx = 3;
    unknown.data.id = 0x456;
    unknown.data.frame_flags = kCanFrameFlagEff;
    unknown.data.len = 2;
    unknown.data.data[0] = 0x5A;
    unknown.data.data[1] = 0xC3;

    adapter_->SimulateOnMessage({registered, unknown});

    ASSERT_EQ(raw_messages.size(), 2u);
    EXPECT_EQ(raw_messages[0].bus_idx, registered.bus_idx);
    EXPECT_EQ(raw_messages[0].data, registered.data);
    EXPECT_EQ(raw_messages[1].bus_idx, unknown.bus_idx);
    EXPECT_EQ(raw_messages[1].data, unknown.data);
    EXPECT_EQ(deliveries.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(received_flags, 0u);
}

TEST_F(AdapterRoutingTest, RoutesAreIsolatedByAdapterAndRawBus) {
    auto& manager = EncosDriverManager::Instance();
    auto* same_adapter_bus = manager.CreateBus(adapter_, 4);
    const auto second_name = "adapter-routing-second-" + std::to_string(next_id_++);
    auto* second_adapter =
        static_cast<TestAdapter*>(manager.CreateAdapterWithFactory(second_name, [second_name]() {
            return new TestAdapter(second_name);
        }));
    auto* second_bus = manager.CreateBus(second_adapter, 3);
    auto* first_battery = manager.CreateBattery(bus_, 1);
    auto* same_adapter_battery = manager.CreateBattery(same_adapter_bus, 1);
    auto* second_adapter_battery = manager.CreateBattery(second_bus, 1);
    ASSERT_NE(first_battery, nullptr);
    ASSERT_NE(same_adapter_battery, nullptr);
    ASSERT_NE(second_adapter_battery, nullptr);
    const std::vector<std::uint32_t> ids{0x3F5, 0x2F5, 0x0F5, 0x1F5};
    std::atomic<unsigned> first_deliveries{0};
    std::atomic<unsigned> same_adapter_deliveries{0};
    std::atomic<unsigned> second_adapter_deliveries{0};
    first_battery->SetOnStatus([&](const BatteryStatus&) {
        first_deliveries.fetch_add(1);
    });
    same_adapter_battery->SetOnStatus([&](const BatteryStatus&) {
        same_adapter_deliveries.fetch_add(1);
    });
    second_adapter_battery->SetOnStatus([&](const BatteryStatus&) {
        second_adapter_deliveries.fetch_add(1);
    });

    MotorMessage first_message{};
    first_message.bus_idx = 3;
    first_message.data.id = ids.front();
    first_message.data.len = 8;
    first_message.data.data[1] = 1;
    MotorMessage same_adapter_message = first_message;
    same_adapter_message.bus_idx = 4;
    adapter_->SimulateOnMessage({first_message, same_adapter_message});
    second_adapter->SimulateOnMessage({first_message});

    EXPECT_EQ(first_deliveries.load(), 1u);
    EXPECT_EQ(same_adapter_deliveries.load(), 1u);
    EXPECT_EQ(second_adapter_deliveries.load(), 1u);
}

TEST_F(AdapterRoutingTest, SparseNegativeRawBusIndexKeepsRouteIdentity) {
    auto& manager = EncosDriverManager::Instance();
    constexpr int kRawBusIndex = -17;
    constexpr std::uint32_t kMotorId = 0x501;
    auto* sparse_bus = manager.CreateBus(adapter_, kRawBusIndex);
    auto* motor =
        manager.CreateMotor(sparse_bus, static_cast<int>(kMotorId), MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    std::atomic<unsigned> status_updates{0};
    motor->SetOnStatus([&](const MotorStatus&) {
        status_updates.fetch_add(1, std::memory_order_relaxed);
    });

    const auto message = MakeStatusMessage(kMotorId);
    EXPECT_TRUE(manager.DispatchReceive(adapter_, kRawBusIndex, message));
    EXPECT_EQ(status_updates.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(EncosDriverManager::MakeReceiveUniqueId(kRawBusIndex, kMotorId),
              (std::uint64_t{static_cast<std::uint32_t>(kRawBusIndex)} << 32u) | kMotorId);
}

}  // namespace
}  // namespace encos
