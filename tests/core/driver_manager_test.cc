#include "encos/driver_manager.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <gtest/gtest.h>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include "adapter/base_adapter.h"
#include "battery/battery.h"
#include "bus/bus.h"
#include "driver_manager_test_access.h"
#include "imu/imu.h"
#include "motor/motor.h"
#include "pms/pms.h"
#include "test_adapter.h"
#include "wait_observer.h"

namespace encos {
namespace {

constexpr auto kAsyncReadyTimeout = std::chrono::seconds(3);
std::string NextManagerTestName(const std::string& prefix) {
    static std::atomic<unsigned> next_id{0};
    return prefix + std::to_string(next_id.fetch_add(1, std::memory_order_relaxed));
}

class DriverManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        DriverManagerTestAccess::Reset(EncosDriverManager::Instance());
        const auto interface_name = "driver-manager-test-" + std::to_string(next_id_++);
        adapter_ =
            static_cast<TestAdapter*>(EncosDriverManager::Instance().CreateAdapterWithFactory(
                interface_name, [interface_name]() {
                    return new TestAdapter(interface_name);
                }));
        bus_ = EncosDriverManager::Instance().CreateBus(adapter_, 3);
    }

    void TearDown() override {
        DriverManagerTestAccess::Reset(EncosDriverManager::Instance());
    }

    static unsigned next_id_;
    TestAdapter* adapter_ = nullptr;
    Bus* bus_ = nullptr;
};

class DriverManagerHotPathTest : public DriverManagerTest {};

unsigned DriverManagerTest::next_id_ = 0;

class ReusedAddressTestAdapter final : public TestAdapter {
public:
    using TestAdapter::TestAdapter;

    static void* operator new(std::size_t size) {
        if (storage_ == nullptr) {
            storage_ = ::operator new(size);
        }
        if (occupied_) {
            throw std::bad_alloc();
        }
        occupied_ = true;
        return storage_;
    }

    static void operator delete(void*) noexcept {
        occupied_ = false;
    }

private:
    static void* storage_;
    static bool occupied_;
};

void* ReusedAddressTestAdapter::storage_ = nullptr;
bool ReusedAddressTestAdapter::occupied_ = false;

class BlockingSendAdapter final : public TestAdapter {
public:
    using TestAdapter::TestAdapter;

    void Send(const MotorMessages& messages) override {
        for (const auto& message : messages) {
            Send(message);
        }
    }

    void Send(const MotorMessage& message) override {
        {
            std::unique_lock<std::mutex> lock(mutex);
            send_entered = true;
            condition.notify_all();
            condition.wait(lock, [this] {
                return release_send;
            });
        }
        TestAdapter::Send(message);
    }

    std::mutex mutex;
    std::condition_variable condition;
    bool send_entered = false;
    bool release_send = false;
};

MotorPackMsg MakeMotorStatusMessage(std::uint32_t id) {
    MotorPackMsg message{};
    message.id = id;
    message.len = 8;
    message.data[0] = 1u << 5u;
    message.data[6] = 50;
    message.data[7] = 50;
    return message;
}

TEST(DriverManagerTests, SingletonAndRawPointerAliasesArePublic) {
    EXPECT_EQ(&EncosDriverManager::Instance(), &EncosDriverManager::Instance());
    static_assert(std::is_same_v<BaseAdapterPtr, BaseAdapter*>);
}

TEST(DriverManagerTests, ReceiveUniqueIdPreservesSignedBusBitsWithoutSignedShift) {
    constexpr int kBusIndex = -0x1234;
    constexpr std::uint32_t kCanId = 0x98ABCDEFu;
    EXPECT_EQ(EncosDriverManager::MakeReceiveUniqueId(kBusIndex, kCanId),
              (std::uint64_t{static_cast<std::uint32_t>(kBusIndex)} << 32u) | kCanId);
}

TEST_F(DriverManagerHotPathTest, MotorControlDoesNotWaitForManagerSlowPathLocks) {
    auto& manager = EncosDriverManager::Instance();
    auto* motor = manager.CreateMotor(bus_, 0x421, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);

    std::mutex mutex;
    std::condition_variable condition;
    bool locks_held = false;
    bool release_locks = false;
    auto lock_holder = std::async(std::launch::async, [&] {
        DriverManagerTestAccess::RunWithSlowPathLocks(manager, [&] {
            std::unique_lock<std::mutex> lock(mutex);
            locks_held = true;
            condition.notify_all();
            condition.wait(lock, [&] {
                return release_locks;
            });
        });
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(1), [&] {
            return locks_held;
        }));
    }

    auto control = std::async(std::launch::async, [motor] {
        motor->SpdControl<0>(1.0F, 1.0F);
    });
    const auto status = control.wait_for(kAsyncReadyTimeout);

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_locks = true;
    }
    condition.notify_all();
    lock_holder.get();
    control.get();
    EXPECT_EQ(status, std::future_status::ready);
}

TEST_F(DriverManagerHotPathTest, AdapterReceiveDoesNotWaitForManagerSlowPathLocks) {
    auto& manager = EncosDriverManager::Instance();
    auto* battery = manager.CreateBattery(bus_, 5);
    ASSERT_NE(battery, nullptr);

    std::atomic<bool> delivered{false};
    battery->SetOnStatus([&delivered](const BatteryStatus&) {
        delivered.store(true, std::memory_order_release);
    });

    std::mutex mutex;
    std::condition_variable condition;
    bool locks_held = false;
    bool release_locks = false;
    auto lock_holder = std::async(std::launch::async, [&] {
        DriverManagerTestAccess::RunWithSlowPathLocks(manager, [&] {
            std::unique_lock<std::mutex> lock(mutex);
            locks_held = true;
            condition.notify_all();
            condition.wait(lock, [&] {
                return release_locks;
            });
        });
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(1), [&] {
            return locks_held;
        }));
    }

    MotorMessage message{};
    message.bus_idx = 3;
    message.data.id = 0x3F9;
    message.data.len = 8;
    message.data.data[1] = 1;
    auto receive = std::async(std::launch::async, [&] {
        adapter_->SimulateOnMessage({message});
    });
    const auto status = receive.wait_for(kAsyncReadyTimeout);

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_locks = true;
    }
    condition.notify_all();
    lock_holder.get();
    receive.get();
    EXPECT_EQ(status, std::future_status::ready);
    EXPECT_TRUE(delivered.load(std::memory_order_acquire));
}

TEST_F(DriverManagerHotPathTest, RetiringDeviceDoesNotBlockIndependentObjects) {
    auto& manager = EncosDriverManager::Instance();
    const auto interface_name = NextManagerTestName("paused-device-operation-");
    auto* blocked_adapter = static_cast<BlockingSendAdapter*>(
        manager.CreateAdapterWithFactory(interface_name, [interface_name] {
            return new BlockingSendAdapter(interface_name);
        }));
    auto* blocked_bus = manager.CreateBus(blocked_adapter, 0);
    auto* blocked_battery = manager.CreateBattery(blocked_bus, 0);
    auto* independent_motor = manager.CreateMotor(bus_, 0x422, MotorModel::EC_A4310_P2);
    auto* independent_battery = manager.CreateBattery(bus_, 6);
    ASSERT_NE(blocked_battery, nullptr);
    ASSERT_NE(independent_motor, nullptr);
    ASSERT_NE(independent_battery, nullptr);
    std::atomic<bool> independent_receive_delivered{false};
    independent_battery->SetOnStatus([&](const BatteryStatus&) {
        independent_receive_delivered.store(true, std::memory_order_release);
    });

    auto blocked_operation = std::async(std::launch::async, [blocked_battery] {
        blocked_battery->RequestCharging(true);
    });
    {
        std::unique_lock<std::mutex> lock(blocked_adapter->mutex);
        ASSERT_TRUE(
            blocked_adapter->condition.wait_for(lock, std::chrono::seconds(1), [blocked_adapter] {
                return blocked_adapter->send_entered;
            }));
    }
    auto deletion = std::async(std::launch::async, [&] {
        return manager.DestroyBattery(blocked_battery);
    });
    EXPECT_EQ(deletion.wait_for(kAsyncReadyTimeout), std::future_status::timeout);

    auto independent_control = std::async(std::launch::async, [independent_motor] {
        independent_motor->SpdControl<0>(1.0F, 1.0F);
    });
    auto independent_bus = std::async(std::launch::async, [this] {
        return bus_->GetBusIndex();
    });
    MotorMessage independent_message{};
    independent_message.bus_idx = 3;
    independent_message.data.id = 0x3FA;
    independent_message.data.len = 8;
    independent_message.data.data[1] = 1;
    auto independent_receive = std::async(std::launch::async, [&] {
        adapter_->SimulateOnMessage({independent_message});
    });
    EXPECT_EQ(independent_control.wait_for(kAsyncReadyTimeout), std::future_status::ready);
    EXPECT_EQ(independent_bus.wait_for(kAsyncReadyTimeout), std::future_status::ready);
    EXPECT_EQ(independent_receive.wait_for(kAsyncReadyTimeout), std::future_status::ready);
    independent_control.get();
    EXPECT_EQ(independent_bus.get(), 3);
    independent_receive.get();
    EXPECT_TRUE(independent_receive_delivered.load(std::memory_order_acquire));

    {
        std::lock_guard<std::mutex> lock(blocked_adapter->mutex);
        blocked_adapter->release_send = true;
    }
    blocked_adapter->condition.notify_all();
    blocked_operation.get();
    EXPECT_TRUE(deletion.get());
}

TEST_F(DriverManagerTest, DirectBatteryRouteRejectsExternalCallbackInstallation) {
    auto* battery = EncosDriverManager::Instance().CreateBattery(bus_, 0);
    ASSERT_NE(battery, nullptr);
    const std::vector<std::uint32_t> ids{0x3F4, 0x2F4, 0x0F4, 0x1F4};

    std::atomic<unsigned> deliveries{0};
    battery->SetOnStatus([&deliveries](const BatteryStatus&) {
        deliveries.fetch_add(1, std::memory_order_relaxed);
    });
    EXPECT_FALSE(DriverManagerTestAccess::RegisterReceiveRoutes(
        EncosDriverManager::Instance(), battery, adapter_, bus_, ids,
        [&deliveries](const MotorPackMsg&) {
            deliveries.fetch_add(1, std::memory_order_relaxed);
        }));

    MotorPackMsg message{};
    message.id = 0x3F4;
    message.len = 8;
    message.data[1] = 1;
    EXPECT_TRUE(EncosDriverManager::Instance().DispatchReceive(adapter_, 3, message));
    EXPECT_EQ(deliveries.load(std::memory_order_relaxed), 1u);
    EXPECT_FALSE(DriverManagerTestAccess::RegisterReceiveRoutes(
        EncosDriverManager::Instance(), battery, adapter_, bus_, ids, [](const MotorPackMsg&) {}));
}

TEST_F(DriverManagerTest, DirectBatteryRouteOwnsEveryReportId) {
    auto* battery = EncosDriverManager::Instance().CreateBattery(bus_, 2);
    ASSERT_NE(battery, nullptr);

    std::atomic<unsigned> deliveries{0};
    const std::vector<std::uint32_t> ids{0x3F6, 0x2F6, 0x0F6, 0x1F6};
    battery->SetOnStatus([&deliveries](const BatteryStatus&) {
        deliveries.fetch_add(1, std::memory_order_relaxed);
    });
    EXPECT_FALSE(DriverManagerTestAccess::RegisterReceiveRoutes(
        EncosDriverManager::Instance(), battery, adapter_, bus_, ids,
        [&deliveries](const MotorPackMsg&) {
            deliveries.fetch_add(1, std::memory_order_relaxed);
        }));
    for (std::size_t index = 0; index < ids.size(); ++index) {
        MotorPackMsg message{};
        message.id = ids[index];
        message.len = 8;
        message.data[0] = index == 2 ? 1 : 0;
        message.data[1] = index == 3 ? 1 : static_cast<uint8_t>(index + 1);
        EXPECT_TRUE(EncosDriverManager::Instance().DispatchReceive(adapter_, 3, message));
    }
    EXPECT_EQ(deliveries.load(std::memory_order_relaxed), ids.size());
}

TEST_F(DriverManagerTest, CallbackCanDeleteAnUnrelatedSiblingDevice) {
    auto* first = EncosDriverManager::Instance().CreateBattery(bus_, 1);
    auto* sibling_bus = EncosDriverManager::Instance().CreateBus(adapter_, 4);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(sibling_bus, nullptr);

    std::atomic<bool> deleted{false};
    first->SetOnStatus([&deleted, sibling_bus](const BatteryStatus&) {
        deleted.store(EncosDriverManager::Instance().DestroyBus(sibling_bus),
                      std::memory_order_release);
    });

    MotorMessage message{};
    message.bus_idx = 3;
    message.data.id = 0x3F5;
    message.data.len = 8;
    message.data.data[1] = 1;
    adapter_->SimulateOnMessage({message});
    EXPECT_TRUE(deleted.load(std::memory_order_acquire));
    EXPECT_EQ(EncosDriverManager::Instance().GetBuses(adapter_).count(4), 0u);
}

TEST_F(DriverManagerTest, CallbackExceptionDoesNotEscapeDispatch) {
    auto* battery = EncosDriverManager::Instance().CreateBattery(bus_, 8);
    ASSERT_NE(battery, nullptr);
    const std::vector<std::uint32_t> ids{0x3FC, 0x2FC, 0x0FC, 0x1FC};
    battery->SetOnStatus([](const BatteryStatus&) {
        throw std::runtime_error("expected callback failure");
    });

    MotorPackMsg message{};
    message.id = ids.front();
    message.len = 8;
    message.data[1] = 1;
    EXPECT_NO_THROW(
        EXPECT_TRUE(EncosDriverManager::Instance().DispatchReceive(adapter_, 3, message)));
}

TEST_F(DriverManagerTest, DeviceDeletionHidesSnapshotsAndCreationWaitsForReplacement) {
    auto& manager = EncosDriverManager::Instance();
    auto* motor = manager.CreateMotor(bus_, 0x41A, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);

    std::mutex mutex;
    std::condition_variable condition;
    bool deletion_blocked = false;
    bool release_deletion = false;
    DriverManagerTestAccess::SetDeletionHook(manager, [&](EncosDriverManager::DeletionStage stage) {
        if (stage != EncosDriverManager::DeletionStage::BeforeDeviceDestroy) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex);
        deletion_blocked = true;
        condition.notify_all();
        condition.wait(lock, [&] {
            return release_deletion;
        });
    });

    auto deletion = std::async(std::launch::async, [&] {
        return manager.DestroyMotor(motor);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(1), [&] {
            return deletion_blocked;
        }));
    }

    EXPECT_EQ(manager.FindMotor(bus_, 0x41A), nullptr);
    EXPECT_EQ(manager.GetMotors(bus_).count(0x41A), 0u);
    WaitObserver wait_observer;
    DriverManagerTestAccess::SetWaitHook(manager, [&] {
        wait_observer.Notify();
    });
    auto recreation = std::async(std::launch::async, [&] {
        return manager.CreateMotor(bus_, 0x41A, MotorModel::EC_A4310_P2);
    });
    wait_observer.WaitForCount(1U);
    DriverManagerTestAccess::SetWaitHook(manager, {});

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_deletion = true;
    }
    condition.notify_all();
    ASSERT_TRUE(deletion.get());
    auto* replacement = recreation.get();
    DriverManagerTestAccess::SetDeletionHook(manager, {});
    EXPECT_NE(replacement, nullptr);
    EXPECT_EQ(manager.FindMotor(bus_, 0x41A), replacement);
}

TEST_F(DriverManagerTest, BusDeletionHidesSnapshotsAndCreationWaitsForReplacement) {
    auto& manager = EncosDriverManager::Instance();
    auto* retiring_bus = manager.CreateBus(adapter_, 9);
    ASSERT_NE(retiring_bus, nullptr);

    std::mutex mutex;
    std::condition_variable condition;
    bool deletion_blocked = false;
    bool release_deletion = false;
    DriverManagerTestAccess::SetDeletionHook(manager, [&](EncosDriverManager::DeletionStage stage) {
        if (stage != EncosDriverManager::DeletionStage::BeforeBusDestroy) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex);
        deletion_blocked = true;
        condition.notify_all();
        condition.wait(lock, [&] {
            return release_deletion;
        });
    });

    auto deletion = std::async(std::launch::async, [&] {
        return manager.DestroyBus(retiring_bus);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(1), [&] {
            return deletion_blocked;
        }));
    }

    EXPECT_EQ(manager.GetBuses(adapter_).count(9), 0u);
    WaitObserver wait_observer;
    DriverManagerTestAccess::SetWaitHook(manager, [&] {
        wait_observer.Notify();
    });
    auto recreation = std::async(std::launch::async, [&] {
        return manager.CreateBus(adapter_, 9);
    });
    wait_observer.WaitForCount(1U);
    DriverManagerTestAccess::SetWaitHook(manager, {});

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_deletion = true;
    }
    condition.notify_all();
    ASSERT_TRUE(deletion.get());
    auto* replacement = recreation.get();
    DriverManagerTestAccess::SetDeletionHook(manager, {});
    EXPECT_NE(replacement, nullptr);
    EXPECT_EQ(manager.GetBuses(adapter_).at(9), replacement);
}

TEST_F(DriverManagerTest, AdapterDeletionHidesDescendantsAndCreationWaitsForReplacement) {
    auto& manager = EncosDriverManager::Instance();
    auto* motor = manager.CreateMotor(bus_, 0x41B, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    const auto interface_name = adapter_->GetInterfaceName();

    std::mutex mutex;
    std::condition_variable condition;
    bool deletion_blocked = false;
    bool release_deletion = false;
    DriverManagerTestAccess::SetDeletionHook(manager, [&](EncosDriverManager::DeletionStage stage) {
        if (stage != EncosDriverManager::DeletionStage::BeforeAdapterReceiveDrain) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex);
        deletion_blocked = true;
        condition.notify_all();
        condition.wait(lock, [&] {
            return release_deletion;
        });
    });

    auto deletion = std::async(std::launch::async, [&] {
        return manager.DestroyAdapter(adapter_);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(1), [&] {
            return deletion_blocked;
        }));
    }

    EXPECT_TRUE(manager.GetBuses(adapter_).empty());
    EXPECT_TRUE(manager.GetMotors(bus_).empty());
    EXPECT_EQ(manager.FindMotor(bus_, 0x41B), nullptr);
    WaitObserver wait_observer;
    DriverManagerTestAccess::SetWaitHook(manager, [&] {
        wait_observer.Notify();
    });
    auto recreation = std::async(std::launch::async, [&] {
        return manager.CreateAdapterWithFactory(interface_name, [interface_name] {
            return new TestAdapter(interface_name);
        });
    });
    wait_observer.WaitForCount(1U);
    DriverManagerTestAccess::SetWaitHook(manager, {});

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_deletion = true;
    }
    condition.notify_all();
    ASSERT_TRUE(deletion.get());
    auto* replacement = recreation.get();
    DriverManagerTestAccess::SetDeletionHook(manager, {});
    EXPECT_NE(replacement, nullptr);
    adapter_ = static_cast<TestAdapter*>(replacement);
    bus_ = manager.CreateBus(adapter_, 3);
}

TEST_F(DriverManagerTest, DeviceDeletionWaitsForDirectPublicMethod) {
    auto& manager = EncosDriverManager::Instance();
    const auto interface_name = NextManagerTestName("blocking-device-operation-");
    auto* adapter = static_cast<BlockingSendAdapter*>(
        manager.CreateAdapterWithFactory(interface_name, [interface_name] {
            return new BlockingSendAdapter(interface_name);
        }));
    auto* bus = manager.CreateBus(adapter, 0);
    auto* battery = manager.CreateBattery(bus, 0);
    ASSERT_NE(battery, nullptr);

    auto send = std::async(std::launch::async, [battery] {
        battery->RequestCharging(true);
    });
    {
        std::unique_lock<std::mutex> lock(adapter->mutex);
        ASSERT_TRUE(adapter->condition.wait_for(lock, std::chrono::seconds(1), [adapter] {
            return adapter->send_entered;
        }));
    }

    auto deletion = std::async(std::launch::async, [&manager, battery] {
        return manager.DestroyBattery(battery);
    });
    EXPECT_EQ(send.wait_for(kAsyncReadyTimeout), std::future_status::timeout);
    EXPECT_EQ(deletion.wait_for(kAsyncReadyTimeout), std::future_status::timeout);

    {
        std::lock_guard<std::mutex> lock(adapter->mutex);
        adapter->release_send = true;
    }
    adapter->condition.notify_all();
    send.get();
    EXPECT_TRUE(deletion.get());
}

TEST_F(DriverManagerTest, CrossCallbackDeletionCycleIsRejectedWithoutDeadlock) {
    auto& manager = EncosDriverManager::Instance();
    auto* first = manager.CreateBattery(bus_, 20);
    const auto interface_name = NextManagerTestName("callback-cycle-");
    auto* second_adapter = static_cast<TestAdapter*>(
        manager.CreateAdapterWithFactory(interface_name, [interface_name] {
            return new TestAdapter(interface_name);
        }));
    auto* second_bus = manager.CreateBus(second_adapter, 0);
    auto* second = manager.CreateBattery(second_bus, 21);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    std::mutex mutex;
    std::condition_variable condition;
    int callbacks_entered = 0;
    int checks_completed = 0;
    const auto rendezvous = [&]() {
        std::unique_lock<std::mutex> lock(mutex);
        ++callbacks_entered;
        condition.notify_all();
        condition.wait(lock, [&] {
            return callbacks_entered == 2;
        });
    };
    const auto keep_callbacks_in_flight = [&]() {
        std::unique_lock<std::mutex> lock(mutex);
        ++checks_completed;
        condition.notify_all();
        condition.wait(lock, [&] {
            return checks_completed == 2;
        });
    };
    std::atomic<bool> first_result{true};
    std::atomic<bool> second_result{true};
    first->SetOnStatus([&](const BatteryStatus&) {
        rendezvous();
        first_result.store(manager.DestroyBattery(second), std::memory_order_release);
        keep_callbacks_in_flight();
    });
    second->SetOnStatus([&](const BatteryStatus&) {
        rendezvous();
        second_result.store(manager.DestroyBattery(first), std::memory_order_release);
        keep_callbacks_in_flight();
    });

    MotorMessage first_message{};
    first_message.bus_idx = 3;
    first_message.data.id = 0x3F4u + 20u;
    first_message.data.len = 8;
    first_message.data.data[1] = 1;
    MotorMessage second_message{};
    second_message.bus_idx = 0;
    second_message.data.id = 0x3F4u + 21u;
    second_message.data.len = 8;
    second_message.data.data[1] = 1;

    std::thread first_receive([&] {
        adapter_->SimulateOnMessage({first_message});
    });
    std::thread second_receive([&] {
        second_adapter->SimulateOnMessage({second_message});
    });
    first_receive.join();
    second_receive.join();

    EXPECT_FALSE(first_result.load(std::memory_order_acquire));
    EXPECT_FALSE(second_result.load(std::memory_order_acquire));
    EXPECT_TRUE(manager.DestroyBattery(first));
    EXPECT_TRUE(manager.DestroyBattery(second));
}

TEST_F(DriverManagerTest, AdapterOnMessageRoutesRegisteredFramesSerially) {
    auto& manager = EncosDriverManager::Instance();
    auto* battery = manager.CreateBattery(bus_, 9);
    ASSERT_NE(battery, nullptr);
    const std::vector<std::uint32_t> ids{0x3FD, 0x2FD, 0x0FD, 0x1FD};

    std::atomic<unsigned> deliveries{0};
    std::atomic<unsigned> active_callbacks{0};
    std::atomic<unsigned> maximum_active_callbacks{0};
    battery->SetOnStatus([&](const BatteryStatus&) {
        const auto active = active_callbacks.fetch_add(1, std::memory_order_acq_rel) + 1;
        auto maximum = maximum_active_callbacks.load(std::memory_order_acquire);
        while (maximum < active &&
               !maximum_active_callbacks.compare_exchange_weak(
                   maximum, active, std::memory_order_acq_rel, std::memory_order_acquire)) {}
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

TEST_F(DriverManagerTest, AdapterOnMessageDropsUnknownBusWithoutCreatingTraffic) {
    MotorMessage message{};
    message.bus_idx = 0x7FFF;
    message.data.id = 0x426;
    message.data.len = 1;
    adapter_->SimulateOnMessage({message});
}

TEST_F(DriverManagerTest, AdapterOnMessageDoesNotPlaceUnregisteredFrameInLegacyBuffers) {
    MotorMessage message{};
    message.bus_idx = 3;
    message.data.id = 0x426;
    message.data.len = 1;
    adapter_->SimulateOnMessage({message});
}

TEST(DriverManagerOwnershipTests, ConcurrentCreationDeduplicatesEveryManagedObjectType) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::Reset(manager);
    const auto interface_name = NextManagerTestName("driver-manager-concurrent-");
    std::array<BaseAdapter*, 8> adapters{};
    std::array<Bus*, 8> buses{};
    std::array<Motor*, 8> motors{};
    std::array<Battery*, 8> batteries{};
    std::array<Imu*, 8> imus{};
    std::array<Pms*, 8> pms{};
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;

    for (std::size_t index = 0; index < adapters.size(); ++index) {
        threads.emplace_back([&, index]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            adapters[index] = manager.CreateAdapterWithFactory(interface_name, [interface_name]() {
                return new TestAdapter(interface_name);
            });
            buses[index] = manager.CreateBus(adapters[index], 17);
            motors[index] = manager.CreateMotor(buses[index], 11, MotorModel::EC_A4310_P2);
            batteries[index] = manager.CreateBattery(buses[index], 2);
            imus[index] = manager.CreateImu(buses[index], 1);
            pms[index] = manager.CreatePms(buses[index]);
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) {
        thread.join();
    }

    for (std::size_t index = 1; index < adapters.size(); ++index) {
        EXPECT_EQ(adapters[index], adapters[0]);
        EXPECT_EQ(buses[index], buses[0]);
        EXPECT_EQ(motors[index], motors[0]);
        EXPECT_EQ(batteries[index], batteries[0]);
        EXPECT_EQ(imus[index], imus[0]);
        EXPECT_EQ(pms[index], pms[0]);
    }
    EXPECT_TRUE(manager.DestroyAdapter(adapters[0]));
}

TEST_F(DriverManagerTest, FactoryAndRouteConflictsRollBackCreation) {
    auto& manager = EncosDriverManager::Instance();
    const auto null_name = NextManagerTestName("driver-manager-null-");
    EXPECT_THROW(manager.CreateAdapterWithFactory(null_name,
                                                  []() {
                                                      return nullptr;
                                                  }),
                 std::runtime_error);
    EXPECT_NE(manager.CreateAdapterWithFactory(null_name,
                                               [null_name]() {
                                                   return new TestAdapter(null_name);
                                               }),
              nullptr);

    const auto throw_name = NextManagerTestName("driver-manager-throw-");
    EXPECT_THROW(manager.CreateAdapterWithFactory(throw_name,
                                                  []() -> BaseAdapter* {
                                                      throw std::runtime_error("factory failure");
                                                  }),
                 std::runtime_error);
    EXPECT_NE(manager.CreateAdapterWithFactory(throw_name,
                                               [throw_name]() {
                                                   return new TestAdapter(throw_name);
                                               }),
              nullptr);

    auto* motor = manager.CreateMotor(bus_, 0x3F4, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    EXPECT_THROW(manager.CreateBattery(bus_, 0), std::runtime_error);
    EXPECT_EQ(manager.FindMotor(bus_, 0x3F4), motor);
    EXPECT_TRUE(manager.DestroyMotor(motor));
    EXPECT_NE(manager.CreateBattery(bus_, 0), nullptr);
}

TEST_F(DriverManagerTest, BoundWriterSanitizesFlagsAndKeepsBusIdentity) {
    auto* pms = EncosDriverManager::Instance().CreatePms(bus_);
    ASSERT_NE(pms, nullptr);

    pms->SendCommand(PmsCommand::EnableChannel1);

    ASSERT_TRUE(adapter_->WaitForSentCount(1));
    const auto sent = adapter_->GetSentMessages();
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent.front().bus_idx, 3);
    EXPECT_EQ(sent.front().data.frame_flags, SanitizeCanFrameFlags(sent.front().data.frame_flags));
}

TEST_F(DriverManagerTest, DuplicateReceiveIdIsRejectedWithoutChangingExistingRoute) {
    auto& manager = EncosDriverManager::Instance();
    auto* battery = manager.CreateBattery(bus_, 11);
    ASSERT_NE(battery, nullptr);
    const std::vector<std::uint32_t> ids{0x3FF, 0x2FF, 0x0FF, 0x1FF};
    std::atomic<unsigned> first_deliveries{0};
    battery->SetOnStatus([&first_deliveries](const BatteryStatus&) {
        first_deliveries.fetch_add(1, std::memory_order_relaxed);
    });
    EXPECT_FALSE(DriverManagerTestAccess::RegisterReceiveRoutes(
        manager, battery, adapter_, bus_, ids, [&first_deliveries](const MotorPackMsg&) {
            first_deliveries.fetch_add(1, std::memory_order_relaxed);
        }));
    EXPECT_FALSE(DriverManagerTestAccess::RegisterReceiveRoutes(manager, battery, adapter_, bus_,
                                                                ids, [](const MotorPackMsg&) {}));

    MotorPackMsg message{};
    message.id = ids.front();
    message.len = 8;
    message.data[1] = 1;
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, message));
    EXPECT_EQ(first_deliveries.load(std::memory_order_relaxed), 1u);
}

TEST_F(DriverManagerTest, MotorIndexMigrationRejectsConflictsWithoutMutatingIndexesOrRoutes) {
    auto& manager = EncosDriverManager::Instance();
    auto* motor = manager.CreateMotor(bus_, 0x403, MotorModel::EC_A4310_P2);
    auto* conflicting = manager.CreateMotor(bus_, 0x404, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    ASSERT_NE(conflicting, nullptr);
    std::atomic<unsigned> deliveries{0};
    motor->SetOnStatus([&deliveries](const MotorStatus&) {
        deliveries.fetch_add(1, std::memory_order_relaxed);
    });

    EXPECT_FALSE(manager.MigrateMotorIndex(motor, 0x404));
    EXPECT_EQ(manager.FindMotor(bus_, 0x403), motor);
    EXPECT_EQ(manager.FindMotor(bus_, 0x404), conflicting);
    auto old_message = MakeMotorStatusMessage(0x403);
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, old_message));
    EXPECT_EQ(deliveries.load(std::memory_order_relaxed), 1u);

    ASSERT_TRUE(manager.MigrateMotorIndex(motor, 0x405));
    EXPECT_EQ(manager.FindMotor(bus_, 0x403), nullptr);
    EXPECT_EQ(manager.FindMotor(bus_, 0x405), motor);
    EXPECT_FALSE(manager.DispatchReceive(adapter_, 3, old_message));
    auto new_message = MakeMotorStatusMessage(0x405);
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, new_message));
    EXPECT_EQ(deliveries.load(std::memory_order_relaxed), 2u);
}

TEST_F(DriverManagerTest, MotorIndexMigrationRollsBackWhenCurrentRangeMoveFails) {
    auto& manager = EncosDriverManager::Instance();
    auto* motor = manager.CreateMotor(bus_, 0x408, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    std::atomic<unsigned> deliveries{0};
    motor->SetOnStatus([&deliveries](const MotorStatus&) {
        deliveries.fetch_add(1, std::memory_order_relaxed);
    });
    DriverManagerTestAccess::SetMigrationHook(
        manager, [](EncosDriverManager::MigrationStage stage) {
            if (stage == EncosDriverManager::MigrationStage::BeforeCurrentRangeMove) {
                throw std::runtime_error("expected current range move failure");
            }
        });

    EXPECT_FALSE(manager.MigrateMotorIndex(motor, 0x409));
    DriverManagerTestAccess::SetMigrationHook(manager, {});
    EXPECT_EQ(manager.FindMotor(bus_, 0x408), motor);
    EXPECT_EQ(manager.FindMotor(bus_, 0x409), nullptr);
    auto old_message = MakeMotorStatusMessage(0x408);
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, old_message));
    auto new_message = MakeMotorStatusMessage(0x409);
    EXPECT_FALSE(manager.DispatchReceive(adapter_, 3, new_message));
    EXPECT_EQ(deliveries.load(std::memory_order_relaxed), 1u);
}

TEST_F(DriverManagerTest, MotorCreationPublishesLatestStatusConfigurationAndPendingCallback) {
    auto& manager = EncosDriverManager::Instance();
    std::mutex mutex;
    std::condition_variable condition;
    bool hook_entered = false;
    bool release_hook = false;
    std::atomic<unsigned> callbacks{0};

    DriverManagerTestAccess::SetCreationHook(manager, [&](EncosDriverManager::CreationStage stage) {
        if (stage != static_cast<EncosDriverManager::CreationStage>(2)) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex);
        hook_entered = true;
        condition.notify_all();
        condition.wait(lock, [&] {
            return release_hook;
        });
    });
    auto creation = std::async(std::launch::async, [&] {
        return manager.CreateMotor(bus_, 0x40A, MotorModel::EC_A4310_P2);
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, kAsyncReadyTimeout, [&] {
            return hook_entered;
        }));
    }
    adapter_->SetMaxStatusLifeCycle(1);
    adapter_->SetOnStatus(3, 0x40A, [&](const MotorStatus&) {
        callbacks.fetch_add(1, std::memory_order_relaxed);
    });
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_hook = true;
    }
    condition.notify_all();

    auto* motor = creation.get();
    DriverManagerTestAccess::SetCreationHook(manager, {});
    ASSERT_NE(motor, nullptr);
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, MakeMotorStatusMessage(0x40A)));
    EXPECT_EQ(callbacks.load(std::memory_order_relaxed), 1u);
    ASSERT_TRUE(adapter_->GetMotorStatus(3, 0x40A, 1).has_value());
    EXPECT_FALSE(adapter_->GetMotorStatus(3, 0x40A, 0).has_value());
}

TEST_F(DriverManagerTest, MotorIndexMigrationPrefersTargetPendingStatusCallback) {
    auto& manager = EncosDriverManager::Instance();
    auto* motor = manager.CreateMotor(bus_, 0x40B, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    std::atomic<unsigned> source_callbacks{0};
    std::atomic<unsigned> target_callbacks{0};
    adapter_->SetOnStatus(3, 0x40B, [&](const MotorStatus&) {
        source_callbacks.fetch_add(1, std::memory_order_relaxed);
    });
    adapter_->SetOnStatus(3, 0x40C, [&](const MotorStatus&) {
        target_callbacks.fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_TRUE(manager.MigrateMotorIndex(motor, 0x40C));
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, MakeMotorStatusMessage(0x40C)));
    EXPECT_EQ(source_callbacks.load(std::memory_order_relaxed), 0u);
    EXPECT_EQ(target_callbacks.load(std::memory_order_relaxed), 1u);
}

TEST_F(DriverManagerTest, MotorIndexMigrationPreservesDirectMotorStatusCallback) {
    auto& manager = EncosDriverManager::Instance();
    auto* motor = manager.CreateMotor(bus_, 0x40D, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    std::atomic<unsigned> callbacks{0};
    motor->SetOnStatus([&](const MotorStatus&) {
        callbacks.fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_TRUE(manager.MigrateMotorIndex(motor, 0x40E));
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, MakeMotorStatusMessage(0x40E)));
    EXPECT_EQ(callbacks.load(std::memory_order_relaxed), 1u);
}

TEST_F(DriverManagerTest, FailedMotorIndexMigrationKeepsSourceAndTargetPendingCallbacks) {
    auto& manager = EncosDriverManager::Instance();
    auto* motor = manager.CreateMotor(bus_, 0x414, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    std::atomic<unsigned> source_callbacks{0};
    std::atomic<unsigned> target_callbacks{0};
    adapter_->SetOnStatus(3, 0x414, [&](const MotorStatus&) {
        source_callbacks.fetch_add(1, std::memory_order_relaxed);
    });
    adapter_->SetOnStatus(3, 0x415, [&](const MotorStatus&) {
        target_callbacks.fetch_add(1, std::memory_order_relaxed);
    });
    DriverManagerTestAccess::SetMigrationHook(
        manager, [](EncosDriverManager::MigrationStage stage) {
            if (stage == EncosDriverManager::MigrationStage::BeforeCurrentRangeMove) {
                throw std::runtime_error("expected migration failure");
            }
        });

    EXPECT_FALSE(manager.MigrateMotorIndex(motor, 0x415));
    DriverManagerTestAccess::SetMigrationHook(manager, {});
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, MakeMotorStatusMessage(0x414)));
    EXPECT_EQ(source_callbacks.load(std::memory_order_relaxed), 1u);
    ASSERT_NE(manager.CreateMotor(bus_, 0x415, MotorModel::EC_A4310_P2), nullptr);
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, MakeMotorStatusMessage(0x415)));
    EXPECT_EQ(target_callbacks.load(std::memory_order_relaxed), 1u);
}

TEST_F(DriverManagerTest, PendingStatusCallbackAppliesBeforeMotorCreation) {
    auto& manager = EncosDriverManager::Instance();
    std::atomic<unsigned> callbacks{0};
    adapter_->SetOnStatus(3, 0x40F, [&](const MotorStatus&) {
        callbacks.fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_NE(manager.CreateMotor(bus_, 0x40F, MotorModel::EC_A4310_P2), nullptr);
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, MakeMotorStatusMessage(0x40F)));
    EXPECT_EQ(callbacks.load(std::memory_order_relaxed), 1u);
}

TEST_F(DriverManagerTest, StatusCallbackCanReconfigureItsMotor) {
    auto& manager = EncosDriverManager::Instance();
    auto* motor = manager.CreateMotor(bus_, 0x411, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    std::atomic<unsigned> callbacks{0};
    adapter_->SetOnStatus(3, 0x411, [&](const MotorStatus&) {
        callbacks.fetch_add(1, std::memory_order_relaxed);
        adapter_->SetOnStatus(3, 0x411, {});
    });

    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, MakeMotorStatusMessage(0x411)));
    EXPECT_TRUE(manager.DispatchReceive(adapter_, 3, MakeMotorStatusMessage(0x411)));
    EXPECT_EQ(callbacks.load(std::memory_order_relaxed), 1u);
}

TEST_F(DriverManagerTest, StatusConfigurationIsRejectedWhileAdapterDeletionIsInFlight) {
    auto& manager = EncosDriverManager::Instance();
    std::mutex mutex;
    std::condition_variable condition;
    bool hook_entered = false;
    bool release_hook = false;
    DriverManagerTestAccess::SetDeletionHook(manager, [&](EncosDriverManager::DeletionStage stage) {
        if (stage != EncosDriverManager::DeletionStage::BeforeAdapterReceiveDrain) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex);
        hook_entered = true;
        condition.notify_all();
        condition.wait(lock, [&] {
            return release_hook;
        });
    });
    auto deletion = std::async(std::launch::async, [&] {
        return manager.DestroyAdapter(adapter_);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, kAsyncReadyTimeout, [&] {
            return hook_entered;
        }));
    }
    auto configuration = std::async(std::launch::async, [&] {
        adapter_->SetMaxStatusLifeCycle(1);
        adapter_->SetOnStatus(3, 0x412, [](const MotorStatus&) {});
    });
    EXPECT_EQ(configuration.wait_for(kAsyncReadyTimeout), std::future_status::ready);
    configuration.get();
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_hook = true;
    }
    condition.notify_all();
    EXPECT_TRUE(deletion.get());
    adapter_ = nullptr;
    bus_ = nullptr;
    DriverManagerTestAccess::SetDeletionHook(manager, {});
}

TEST_F(DriverManagerTest, AdapterDestructionClearsStatusStateBeforeAddressReuse) {
    auto& manager = EncosDriverManager::Instance();
    std::atomic<unsigned> stale_callbacks{0};
    const auto first_name = NextManagerTestName("status-address-reuse-first-");
    auto* first = static_cast<ReusedAddressTestAdapter*>(
        manager.CreateAdapterWithFactory(first_name, [first_name] {
            return new ReusedAddressTestAdapter(first_name);
        }));
    ASSERT_NE(first, nullptr);
    first->SetMaxStatusLifeCycle(1);
    first->SetOnStatus(7, 0x413, [&](const MotorStatus&) {
        stale_callbacks.fetch_add(1, std::memory_order_relaxed);
    });
    ASSERT_TRUE(manager.DestroyAdapter(first));

    const auto second_name = NextManagerTestName("status-address-reuse-second-");
    auto* second = static_cast<ReusedAddressTestAdapter*>(
        manager.CreateAdapterWithFactory(second_name, [second_name] {
            return new ReusedAddressTestAdapter(second_name);
        }));
    ASSERT_EQ(second, first);
    auto* bus = manager.CreateBus(second, 7);
    auto* motor = manager.CreateMotor(bus, 0x413, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    EXPECT_TRUE(manager.DispatchReceive(second, 7, MakeMotorStatusMessage(0x413)));
    EXPECT_EQ(stale_callbacks.load(std::memory_order_relaxed), 0u);
    ASSERT_TRUE(second->GetMotorStatus(7, 0x413, 1).has_value());
    EXPECT_TRUE(second->GetMotorStatus(7, 0x413, 0).has_value());
    EXPECT_TRUE(manager.DestroyAdapter(second));
}

TEST_F(DriverManagerTest, RouteRegistrationIsRejectedWhileDeviceDeletionIsInFlight) {
    auto& manager = EncosDriverManager::Instance();
    std::mutex hook_mutex;
    std::condition_variable hook_condition;
    bool hook_entered = false;
    bool release_hook = false;
    auto* motor = manager.CreateMotor(bus_, 0x406, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    DriverManagerTestAccess::SetDeletionHook(manager, [&](EncosDriverManager::DeletionStage stage) {
        if (stage != EncosDriverManager::DeletionStage::BeforeDeviceDestroy) {
            return;
        }
        std::unique_lock<std::mutex> lock(hook_mutex);
        hook_entered = true;
        hook_condition.notify_all();
        hook_condition.wait(lock, [&]() {
            return release_hook;
        });
    });
    auto deletion = std::async(std::launch::async, [&]() {
        return manager.DestroyMotor(motor);
    });
    {
        std::unique_lock<std::mutex> lock(hook_mutex);
        hook_condition.wait(lock, [&]() {
            return hook_entered;
        });
    }
    EXPECT_FALSE(DriverManagerTestAccess::RegisterReceiveRoutes(
        manager, motor, adapter_, bus_, {0x406}, [](const MotorPackMsg&) {}));
    {
        std::lock_guard<std::mutex> lock(hook_mutex);
        release_hook = true;
    }
    hook_condition.notify_all();
    EXPECT_TRUE(deletion.get());
    MotorPackMsg message{};
    message.id = 0x406;
    EXPECT_FALSE(manager.DispatchReceive(adapter_, 3, message));
    DriverManagerTestAccess::SetDeletionHook(manager, {});
}

TEST_F(DriverManagerTest, FailedBusPublicationUnregistersItsKnownBusIndex) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::SetCreationHook(manager, [](EncosDriverManager::CreationStage stage) {
        if (stage == EncosDriverManager::CreationStage::BeforeBusPublish) {
            throw std::runtime_error("expected bus publication failure");
        }
    });
    EXPECT_THROW(manager.CreateBus(adapter_, 0x407), std::runtime_error);
    DriverManagerTestAccess::SetCreationHook(manager, {});

    MotorMessage message{};
    message.bus_idx = 0x407;
    message.data.id = 1;
    message.data.len = 8;
    adapter_->SimulateOnMessage({message});
}

TEST_F(DriverManagerTest, NewlyPublishedBusInheritsConcurrentAdapterDefaultMode) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::SetCreationHook(
        manager, [this](EncosDriverManager::CreationStage stage) {
            if (stage == EncosDriverManager::CreationStage::BeforeBusPublish) {
                adapter_->SetSyncMode(true);
            }
        });

    auto* bus = manager.CreateBus(adapter_, 0x408);
    DriverManagerTestAccess::SetCreationHook(manager, {});
    auto* motor = manager.CreateMotor(bus, 1, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(1.0F, 2.0F);
    EXPECT_TRUE(adapter_->GetSentMessages().empty());

    bus->Commit();
    ASSERT_EQ(adapter_->GetSentMessages().size(), 1U);
}

TEST_F(DriverManagerTest, BusDeletionUnregistersKnownBusIndexAndAllowsCleanRecreation) {
    auto& manager = EncosDriverManager::Instance();
    auto* transient_bus = manager.CreateBus(adapter_, 0x40A);
    ASSERT_NE(transient_bus, nullptr);
    ASSERT_TRUE(manager.DestroyBus(transient_bus));

    MotorMessage message{};
    message.bus_idx = 0x40A;
    message.data.id = 1;
    message.data.len = 8;
    adapter_->SimulateOnMessage({message});

    transient_bus = manager.CreateBus(adapter_, 0x40A);
    ASSERT_NE(transient_bus, nullptr);
    adapter_->SimulateOnMessage({message});
}

TEST_F(DriverManagerTest, IndividualDeviceDeletionUnregistersRoutesAndAllowsRecreation) {
    auto& manager = EncosDriverManager::Instance();
    auto* motor = manager.CreateMotor(bus_, 0x410, MotorModel::EC_A4310_P2);
    auto* battery = manager.CreateBattery(bus_, 3);
    auto* imu = manager.CreateImu(bus_, 2);
    auto* pms = manager.CreatePms(bus_);
    ASSERT_NE(motor, nullptr);
    ASSERT_NE(battery, nullptr);
    ASSERT_NE(imu, nullptr);
    ASSERT_NE(pms, nullptr);

    EXPECT_TRUE(manager.DestroyMotor(motor));
    EXPECT_TRUE(manager.DestroyBattery(battery));
    EXPECT_TRUE(manager.DestroyImu(imu));
    EXPECT_TRUE(manager.DestroyPms(pms));
    EXPECT_EQ(manager.FindMotor(bus_, 0x410), nullptr);
    MotorPackMsg message{};
    message.id = 0x410;
    EXPECT_FALSE(manager.DispatchReceive(adapter_, 3, message));
    EXPECT_NE(manager.CreateMotor(bus_, 0x410, MotorModel::EC_A4310_P2), nullptr);
    EXPECT_NE(manager.CreateBattery(bus_, 3), nullptr);
    EXPECT_NE(manager.CreateImu(bus_, 2), nullptr);
    EXPECT_NE(manager.CreatePms(bus_), nullptr);
}

TEST_F(DriverManagerTest, BusAndAdapterCascadeDeleteChildrenAndPermitFreshIdentity) {
    auto& manager = EncosDriverManager::Instance();
    auto* second_bus = manager.CreateBus(adapter_, 4);
    ASSERT_NE(second_bus, nullptr);
    ASSERT_NE(manager.CreateMotor(bus_, 1, MotorModel::EC_A4310_P2), nullptr);
    ASSERT_NE(manager.CreateBattery(second_bus, 1), nullptr);

    EXPECT_TRUE(manager.DestroyBus(bus_));
    EXPECT_TRUE(manager.GetMotors(bus_).empty());
    EXPECT_EQ(manager.GetBuses(adapter_).count(3), 0u);
    EXPECT_NE(manager.CreateBus(adapter_, 3), nullptr);

    EXPECT_TRUE(manager.DestroyAdapter(adapter_));
    EXPECT_TRUE(manager.GetBuses(adapter_).empty());
    const auto recreated_name = NextManagerTestName("driver-manager-recreated-");
    adapter_ = static_cast<TestAdapter*>(
        manager.CreateAdapterWithFactory(recreated_name, [recreated_name]() {
            return new TestAdapter(recreated_name);
        }));
    EXPECT_NE(adapter_, nullptr);
}

TEST_F(DriverManagerTest, RejectsUnknownMismatchedAndCallbackOwnedDeletion) {
    auto& manager = EncosDriverManager::Instance();
    EXPECT_FALSE(manager.DestroyMotor(reinterpret_cast<Motor*>(0x1)));

    auto* battery = manager.CreateBattery(bus_, 4);
    ASSERT_NE(battery, nullptr);
    EXPECT_FALSE(manager.DestroyMotor(reinterpret_cast<Motor*>(battery)));
    EXPECT_TRUE(manager.DestroyBattery(battery));

    auto* callback_battery = manager.CreateBattery(bus_, 5);
    ASSERT_NE(callback_battery, nullptr);
    const std::vector<std::uint32_t> callback_ids{0x3F9, 0x2F9, 0x0F9, 0x1F9};
    std::atomic<bool> rejected_self{false};
    std::atomic<bool> rejected_bus{false};
    std::atomic<bool> rejected_adapter{false};
    callback_battery->SetOnStatus([&, callback_battery](const BatteryStatus&) {
        rejected_self.store(!manager.DestroyBattery(callback_battery), std::memory_order_release);
        rejected_bus.store(!manager.DestroyBus(bus_), std::memory_order_release);
        rejected_adapter.store(!manager.DestroyAdapter(adapter_), std::memory_order_release);
    });

    MotorPackMsg message{};
    message.id = callback_ids.front();
    message.len = 8;
    message.data[1] = 1;
    ASSERT_TRUE(manager.DispatchReceive(adapter_, 3, message));
    EXPECT_TRUE(rejected_self.load(std::memory_order_acquire));
    EXPECT_TRUE(rejected_bus.load(std::memory_order_acquire));
    EXPECT_TRUE(rejected_adapter.load(std::memory_order_acquire));
    EXPECT_TRUE(manager.DestroyBattery(callback_battery));
}

TEST_F(DriverManagerTest, DeletionDrainsDirectBatteryCallbacksWithoutHoldingGlobalLock) {
    auto& manager = EncosDriverManager::Instance();
    auto* battery = manager.CreateBattery(bus_, 6);
    ASSERT_NE(battery, nullptr);
    const std::vector<std::uint32_t> ids{0x3FA, 0x2FA, 0x0FA, 0x1FA};
    std::atomic<bool> callback_entered{false};
    std::atomic<bool> release_callback{false};
    battery->SetOnStatus([&callback_entered, &release_callback](const BatteryStatus&) {
        callback_entered.store(true, std::memory_order_release);
        while (!release_callback.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    });

    MotorPackMsg message{};
    message.id = ids.front();
    message.len = 8;
    message.data[1] = 1;
    auto dispatch = std::async(std::launch::async, [&]() {
        return manager.DispatchReceive(adapter_, 3, message);
    });
    while (!callback_entered.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    WaitObserver wait_observer;
    DriverManagerTestAccess::SetWaitHook(manager, [&] {
        wait_observer.Notify();
    });
    auto deletion = std::async(std::launch::async, [&]() {
        return manager.DestroyBattery(battery);
    });
    wait_observer.WaitForCount(1U);
    DriverManagerTestAccess::SetWaitHook(manager, {});
    EXPECT_NE(manager.CreateMotor(bus_, 0x431, MotorModel::EC_A4310_P2), nullptr);

    release_callback.store(true, std::memory_order_release);
    EXPECT_TRUE(dispatch.get());
    EXPECT_TRUE(deletion.get());
}

TEST_F(DriverManagerTest, ParentDeletionWaitsForPendingBusOrDevicePublicationAndRollsBack) {
    auto& manager = EncosDriverManager::Instance();
    std::mutex hook_mutex;
    std::condition_variable hook_condition;
    bool hook_entered = false;
    bool release_hook = false;
    auto block_stage = [&](EncosDriverManager::CreationStage) {
        std::unique_lock<std::mutex> lock(hook_mutex);
        hook_entered = true;
        hook_condition.notify_all();
        hook_condition.wait(lock, [&]() {
            return release_hook;
        });
    };

    const auto adapter_name = NextManagerTestName("driver-manager-pending-bus-");
    auto* pending_adapter = manager.CreateAdapterWithFactory(adapter_name, [adapter_name]() {
        return new TestAdapter(adapter_name);
    });
    DriverManagerTestAccess::SetCreationHook(manager, [&](EncosDriverManager::CreationStage stage) {
        if (stage == EncosDriverManager::CreationStage::BeforeBusPublish) {
            block_stage(stage);
        }
    });
    auto bus_creation = std::async(std::launch::async, [&]() {
        try {
            (void) manager.CreateBus(pending_adapter, 88);
            return false;
        } catch (const std::runtime_error&) {
            return true;
        }
    });
    {
        std::unique_lock<std::mutex> lock(hook_mutex);
        hook_condition.wait(lock, [&]() {
            return hook_entered;
        });
    }
    WaitObserver adapter_wait_observer;
    DriverManagerTestAccess::SetWaitHook(manager, [&] {
        adapter_wait_observer.Notify();
    });
    auto adapter_deletion = std::async(std::launch::async, [&]() {
        return manager.DestroyAdapter(pending_adapter);
    });
    adapter_wait_observer.WaitForCount(1U);
    DriverManagerTestAccess::SetWaitHook(manager, {});
    {
        std::lock_guard<std::mutex> lock(hook_mutex);
        release_hook = true;
    }
    hook_condition.notify_all();
    EXPECT_TRUE(bus_creation.get());
    EXPECT_TRUE(adapter_deletion.get());

    hook_entered = false;
    release_hook = false;
    DriverManagerTestAccess::SetCreationHook(manager, [&](EncosDriverManager::CreationStage stage) {
        if (stage == EncosDriverManager::CreationStage::BeforeDevicePublish) {
            block_stage(stage);
        }
    });
    auto device_creation = std::async(std::launch::async, [&]() {
        try {
            (void) manager.CreateMotor(bus_, 0x440, MotorModel::EC_A4310_P2);
            return false;
        } catch (const std::runtime_error&) {
            return true;
        }
    });
    {
        std::unique_lock<std::mutex> lock(hook_mutex);
        hook_condition.wait(lock, [&]() {
            return hook_entered;
        });
    }
    WaitObserver bus_wait_observer;
    DriverManagerTestAccess::SetWaitHook(manager, [&] {
        bus_wait_observer.Notify();
    });
    auto bus_deletion = std::async(std::launch::async, [&]() {
        return manager.DestroyBus(bus_);
    });
    bus_wait_observer.WaitForCount(1U);
    DriverManagerTestAccess::SetWaitHook(manager, {});
    {
        std::lock_guard<std::mutex> lock(hook_mutex);
        release_hook = true;
    }
    hook_condition.notify_all();
    EXPECT_TRUE(device_creation.get());
    EXPECT_TRUE(bus_deletion.get());
    DriverManagerTestAccess::SetCreationHook(manager, {});
    bus_ = nullptr;
}

TEST_F(DriverManagerTest, ParentCascadeWaitsForAnIndependentlyDeletingChild) {
    auto& manager = EncosDriverManager::Instance();
    std::mutex hook_mutex;
    std::condition_variable hook_condition;
    bool hook_entered = false;
    bool release_hook = false;
    DriverManagerTestAccess::SetDeletionHook(manager, [&](EncosDriverManager::DeletionStage stage) {
        if (stage != EncosDriverManager::DeletionStage::BeforeDeviceDestroy) {
            return;
        }
        std::unique_lock<std::mutex> lock(hook_mutex);
        hook_entered = true;
        hook_condition.notify_all();
        hook_condition.wait(lock, [&]() {
            return release_hook;
        });
    });
    auto* motor = manager.CreateMotor(bus_, 0x450, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    auto motor_deletion = std::async(std::launch::async, [&]() {
        return manager.DestroyMotor(motor);
    });
    {
        std::unique_lock<std::mutex> lock(hook_mutex);
        hook_condition.wait(lock, [&]() {
            return hook_entered;
        });
    }
    WaitObserver wait_observer;
    DriverManagerTestAccess::SetWaitHook(manager, [&] {
        wait_observer.Notify();
    });
    auto bus_deletion = std::async(std::launch::async, [&]() {
        return manager.DestroyBus(bus_);
    });
    wait_observer.WaitForCount(1U);
    DriverManagerTestAccess::SetWaitHook(manager, {});
    {
        std::lock_guard<std::mutex> lock(hook_mutex);
        release_hook = true;
    }
    hook_condition.notify_all();
    EXPECT_TRUE(motor_deletion.get());
    EXPECT_TRUE(bus_deletion.get());
    DriverManagerTestAccess::SetDeletionHook(manager, {});
    bus_ = nullptr;
}

TEST_F(DriverManagerTest, AdapterCascadeWaitsForAnIndependentlyDeletingBusAndDestroysBottomUp) {
    auto& manager = EncosDriverManager::Instance();
    std::mutex hook_mutex;
    std::condition_variable hook_condition;
    bool bus_hook_entered = false;
    bool release_bus_hook = false;
    std::vector<EncosDriverManager::DeletionStage> order;
    DriverManagerTestAccess::SetDeletionHook(manager, [&](EncosDriverManager::DeletionStage stage) {
        std::unique_lock<std::mutex> lock(hook_mutex);
        order.push_back(stage);
        if (stage != EncosDriverManager::DeletionStage::BeforeBusDestroy) {
            return;
        }
        bus_hook_entered = true;
        hook_condition.notify_all();
        hook_condition.wait(lock, [&]() {
            return release_bus_hook;
        });
    });
    ASSERT_NE(manager.CreateMotor(bus_, 0x451, MotorModel::EC_A4310_P2), nullptr);
    auto bus_deletion = std::async(std::launch::async, [&]() {
        return manager.DestroyBus(bus_);
    });
    {
        std::unique_lock<std::mutex> lock(hook_mutex);
        hook_condition.wait(lock, [&]() {
            return bus_hook_entered;
        });
    }
    WaitObserver wait_observer;
    DriverManagerTestAccess::SetWaitHook(manager, [&] {
        wait_observer.Notify();
    });
    auto adapter_deletion = std::async(std::launch::async, [&]() {
        return manager.DestroyAdapter(adapter_);
    });
    wait_observer.WaitForCount(1U);
    DriverManagerTestAccess::SetWaitHook(manager, {});
    {
        std::lock_guard<std::mutex> lock(hook_mutex);
        release_bus_hook = true;
    }
    hook_condition.notify_all();
    EXPECT_TRUE(bus_deletion.get());
    EXPECT_TRUE(adapter_deletion.get());
    DriverManagerTestAccess::SetDeletionHook(manager, {});
    ASSERT_GE(order.size(), 3u);
    EXPECT_EQ(order.front(), EncosDriverManager::DeletionStage::BeforeDeviceDestroy);
    EXPECT_EQ(order[1], EncosDriverManager::DeletionStage::BeforeBusDestroy);
    EXPECT_EQ(order.back(), EncosDriverManager::DeletionStage::BeforeAdapterDestroy);
    adapter_ = nullptr;
    bus_ = nullptr;
}

}  // namespace
}  // namespace encos
