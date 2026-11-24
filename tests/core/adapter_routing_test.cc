// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

#include "bus/bus.h"
#include "device/battery/battery.h"
#include "device/motor/can_address.h"
#include "device/motor/motor.h"
#include "driver_manager_test_access.h"
#include "encos/driver_manager.h"
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

Battery* CreatePacketRoute(Bus* bus, int device_id, std::uint32_t can_id,
                           EncosDriverManager::ReceiveCallback callback) {
    auto& impl = DriverManagerTestAccess::Internals(EncosDriverManager::Instance());
    auto recipe = impl.MakeBatteryRecipe(bus, device_id);
    recipe.can_ids = {can_id};
    const auto bind = recipe.bind;
    recipe.bind = [bind, callback](void* device) {
        auto binding = bind(device);
        binding.callback = callback;
        return binding;
    };
    return static_cast<Battery*>(impl.CreateDevice(bus, device_id, recipe));
}

TEST_F(AdapterRoutingTest, GroupedFdDeliversExactFirstThenDlcSizedSlotsIncludingZeroData) {
    for (const auto length : {54, 63, 64}) {
        const std::uint32_t id = 0x500U + length;
        const int device_id = length * 10;
        std::vector<MotorPackMsg> received;
        const auto collect = [&](const MotorPackMsg& packet) {
            received.push_back(packet);
        };
        ASSERT_NE(CreatePacketRoute(bus_, device_id, id, collect), nullptr);
        ASSERT_NE(CreatePacketRoute(bus_, device_id + 1, MakeCanAddress(id, 1), collect), nullptr);
        ASSERT_NE(CreatePacketRoute(bus_, device_id + 2, MakeCanAddress(id, 4), collect), nullptr);
        ASSERT_NE(CreatePacketRoute(bus_, device_id + 3, MakeCanAddress(id, 7), collect), nullptr);
        MotorMessage message{};
        message.bus_idx = 3;
        message.data.id = id;
        message.data.frame_flags = kCanFrameFlagFdMask | kCanFrameFlagEff;
        message.data.len = static_cast<uint8_t>(length);
        message.data.data[0] = 3;
        std::fill_n(message.data.data + 1, 8, 0xAA);
        message.data.data[27] = 8;
        if (length != 54) {
            message.data.data[54] = 2;
            std::fill_n(message.data.data + 55, 8, 0xBB);
        }
        if (length == 64)
            message.data.data[63] = 0xCC;
        unsigned raw_calls = 0;
        adapter_->SetRawMessageCallbackForTests([&](const MotorMessages& raw) {
            ++raw_calls;
            ASSERT_EQ(raw.size(), 1u);
            EXPECT_EQ(raw.front().data, message.data);
        });
        adapter_->SimulateOnMessage({message});
        EXPECT_EQ(raw_calls, 1u);
        ASSERT_EQ(received.size(), length == 54 ? 3u : 4u);
        EXPECT_EQ(received.front(), message.data);
        const std::array<uint8_t, 3> slots{1, 4, 7};
        for (std::size_t i = 0; i + 1 < received.size(); ++i) {
            MotorPackMsg expected{};
            expected.id = MakeCanAddress(id, slots[i]);
            expected.frame_flags = message.data.frame_flags;
            const auto offset = (slots[i] - 1) * 9;
            expected.len = message.data.data[offset];
            std::copy_n(message.data.data + offset + 1, expected.len, expected.data);
            EXPECT_EQ(received[i + 1], expected);
        }
        received.clear();
        message.data.len = 64;
        message.data.data[54] = 0;
        adapter_->SimulateOnMessage({message});
        EXPECT_EQ(received.size(), 3u);
        received.clear();
        message.data.data[45] = 9;
        adapter_->SimulateOnMessage({message});
        ASSERT_EQ(received.size(), 1u);
        EXPECT_EQ(received.front(), message.data);
        adapter_->SetRawMessageCallbackForTests({});
    }
}

TEST_F(AdapterRoutingTest, GroupedFdRequiresPhysicalIdFdFlagAndExactLength) {
    auto& manager = EncosDriverManager::Instance();
    unsigned deliveries = 0;
    ASSERT_NE(CreatePacketRoute(bus_, 1, MakeCanAddress(0x522, 2),
                                [&](const MotorPackMsg&) {
                                    ++deliveries;
                                }),
              nullptr);
    MotorPackMsg message{};
    message.id = 0x522;
    message.frame_flags = kCanFrameFlagFdMask;
    message.data[9] = 2;
    for (const auto length : {0, 8, 48, 55, 56, 57, 62, 65}) {
        message.len = static_cast<uint8_t>(length);
        EXPECT_FALSE(manager.DispatchReceive(adapter_, 3, message));
    }
    message.len = 54;
    for (const auto flags : {0, int(kCanFrameFlagFdBit1), int(kCanFrameFlagFdBit2)}) {
        message.frame_flags = static_cast<uint8_t>(flags);
        EXPECT_FALSE(manager.DispatchReceive(adapter_, 3, message));
    }
    message.frame_flags = kCanFrameFlagFdMask;
    message.id = MakeCanAddress(0x522, 1);
    EXPECT_FALSE(manager.DispatchReceive(adapter_, 3, message));
    EXPECT_EQ(deliveries, 0u);
    message.id = 0x522;
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, message));
    EXPECT_EQ(deliveries, 1u);
    message.data[45] = 9;
    EXPECT_FALSE(manager.DispatchReceive(adapter_, 3, message));
    EXPECT_EQ(deliveries, 1u);
    message.data[45] = 0;
    message.data[9] = 0;
    EXPECT_FALSE(manager.DispatchReceive(adapter_, 3, message));
    EXPECT_EQ(deliveries, 1u);
    message.id = 0x523;
    EXPECT_FALSE(manager.DispatchReceive(adapter_, 3, message));
}

TEST_F(AdapterRoutingTest, GroupedSystemIdUsesExactAndChildRoutesWithoutBroadcast) {
    auto& manager = EncosDriverManager::Instance();
    std::vector<std::uint32_t> received;
    const auto collect = [&](const MotorPackMsg& packet) {
        received.push_back(packet.id);
    };
    ASSERT_NE(CreatePacketRoute(bus_, 1, 0x7FF, collect), nullptr);
    ASSERT_NE(CreatePacketRoute(bus_, 2, MakeCanAddress(0x7FF, 3), collect), nullptr);
    ASSERT_NE(CreatePacketRoute(bus_, 3, 0x524, collect), nullptr);
    MotorPackMsg message{};
    message.id = 0x7FF;
    message.data[18] = 8;
    message.len = 64;
    message.frame_flags = kCanFrameFlagFdMask;
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, message));
    EXPECT_EQ(received, (std::vector<std::uint32_t>{0x7FF, MakeCanAddress(0x7FF, 3)}));
    received.clear();
    message.len = 8;
    EXPECT_FALSE(manager.DispatchReceive(adapter_, 3, message));
    EXPECT_EQ(received.size(), 3u);
}

TEST_F(AdapterRoutingTest, GroupedCallbackCanDeleteLaterChildBeforeItsRouteIsAcquired) {
    auto& manager = EncosDriverManager::Instance();
    unsigned deliveries = 0;
    auto* child = CreatePacketRoute(bus_, 1, MakeCanAddress(0x525, 1), [&](const MotorPackMsg&) {
        ++deliveries;
    });
    ASSERT_NE(child, nullptr);
    ASSERT_NE(CreatePacketRoute(bus_, 2, 0x525,
                                [&](const MotorPackMsg&) {
                                    EXPECT_TRUE(manager.DestroyBattery(child));
                                }),
              nullptr);
    MotorPackMsg message{};
    message.id = 0x525;
    message.data[0] = 8;
    message.len = 54;
    message.frame_flags = kCanFrameFlagFdMask;
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, message));
    EXPECT_EQ(deliveries, 0u);
}

TEST_F(AdapterRoutingTest, SerializesRegisteredCallbackDelivery) {
    auto& manager = EncosDriverManager::Instance();
    auto* battery = manager.CreateBattery(bus_, 10);
    ASSERT_NE(battery, nullptr);
    const std::vector<std::uint32_t> ids{0x408, 0x308, 0x108, 0x208};

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
    const std::vector<std::uint32_t> ids{0x3F6, 0x2F6, 0x0F6, 0x1F6};
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
