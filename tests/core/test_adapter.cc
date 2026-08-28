#include "test_adapter.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "adapter/base_adapter.h"
#include "base_adapter_test_access.h"
#include "battery/battery.h"
#include "bus/bus.h"
#include "device_status_test_access.h"
#include "driver_manager_test_access.h"
#include "encos/driver_manager.h"
#include "encos/export.h"
#include "imu/imu.h"
#include "motor/motor.h"
#include "motor/types.h"
#include "platform/log.h"
#include "pms/pms.h"
#include "test_fixtures.h"

namespace encos {

namespace {

TestAdapter* MakeManagedTestAdapter(std::string interface_name = {}) {
    static std::atomic<unsigned> next_id{0};
    if (interface_name.empty()) {
        interface_name = "managed-test-" + std::to_string(next_id.fetch_add(1));
    }
    return static_cast<TestAdapter*>(
        EncosDriverManager::Instance().CreateAdapterWithFactory(interface_name, [interface_name]() {
            return new TestAdapter(interface_name);
        }));
}

FakeAdapter* MakeManagedFakeAdapter(std::string interface_name) {
    return static_cast<FakeAdapter*>(
        EncosDriverManager::Instance().CreateAdapterWithFactory(interface_name, [interface_name]() {
            return new FakeAdapter(interface_name);
        }));
}

}  // namespace

TestAdapter::TestAdapter(const std::string& interface_name, const std::string& logger_name)
    : BaseAdapter(interface_name, logger_name.empty() ? "TestAdapter" : logger_name,
                  LogLevel::Info) {}

void TestAdapter::Send(const MotorMessage& message) {
    platform::LockGuard<platform::Mutex> lock(send_mutex_);
    send_buffer_.push_back(message);
    sent_batches_.push_back({message});
    send_condition_.notify_all();
}

void TestAdapter::Send(const MotorMessages& messages) {
    platform::LockGuard<platform::Mutex> lock(send_mutex_);
    send_buffer_.insert(send_buffer_.end(), messages.begin(), messages.end());
    sent_batches_.push_back(messages);
    send_condition_.notify_all();
}

void TestAdapter::SendSynchronized(const MotorMessages& messages) {
    platform::LockGuard<platform::Mutex> lock(send_mutex_);
    send_buffer_.insert(send_buffer_.end(), messages.begin(), messages.end());
    sent_batches_.push_back(messages);
    synchronized_batches_.push_back(messages);
    send_condition_.notify_all();
}

std::vector<MotorMessage> TestAdapter::GetSentMessages() const {
    platform::LockGuard<platform::Mutex> lock(send_mutex_);
    return send_buffer_;
}

std::vector<MotorMessages> TestAdapter::GetSentBatches() const {
    platform::LockGuard<platform::Mutex> lock(send_mutex_);
    return sent_batches_;
}

std::vector<MotorMessages> TestAdapter::GetSynchronizedBatches() const {
    platform::LockGuard<platform::Mutex> lock(send_mutex_);
    return synchronized_batches_;
}

bool TestAdapter::WaitForSentCount(std::size_t count, std::chrono::milliseconds timeout) {
    platform::UniqueLock<platform::Mutex> lock(send_mutex_);
    return send_condition_.wait_for(lock, timeout, [this, count]() {
        return send_buffer_.size() >= count;
    });
}

void TestAdapter::ClearSentMessages() {
    platform::LockGuard<platform::Mutex> lock(send_mutex_);
    send_buffer_.clear();
    sent_batches_.clear();
    synchronized_batches_.clear();
}

std::unordered_map<int, Bus*> TestAdapter::GetBuses() {
    return GetKnownBusesSnapshot();
}

void TestAdapter::SimulateOnMessage(const MotorMessages& messages) {
    OnMessage(messages);
}

void TestAdapter::SetRawMessageCallbackForTests(
    std::function<void(const MotorMessages&)> callback) {
    BaseAdapterTestAccess::SetRawMessageCallback(this, std::move(callback));
}

namespace {

class BlockingBatchAdapter final : public BaseAdapter {
public:
    explicit BlockingBatchAdapter(const std::string& interface_name)
        : BaseAdapter(interface_name, "BlockingBatchAdapter") {}

    ~BlockingBatchAdapter() override {}

    std::unordered_map<int, Bus*> GetBuses() override {
        return GetKnownBusesSnapshot();
    }

    bool Ok() override {
        return true;
    }

    void WaitForFirstSubmission() {
        std::unique_lock<std::mutex> lock(mutex_);
        ASSERT_TRUE(condition_.wait_for(lock, std::chrono::seconds(3), [this]() {
            return !batches_.empty();
        }));
    }

    void ReleaseFirstSubmission() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            release_first_ = true;
        }
        condition_.notify_all();
    }

    std::vector<MotorMessages> GetBatches() {
        std::lock_guard<std::mutex> lock(mutex_);
        return batches_;
    }

protected:
    void Send(const MotorMessage& message) override {
        Send(MotorMessages{message});
    }

    void Send(const MotorMessages& messages) override {
        std::unique_lock<std::mutex> lock(mutex_);
        batches_.push_back(messages);
        condition_.notify_all();
        if (batches_.size() == 1U) {
            condition_.wait(lock, [this]() {
                return release_first_;
            });
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<MotorMessages> batches_;
    bool release_first_ = false;
};

class QueuedPhysicalAdapter final : public BaseAdapter {
public:
    explicit QueuedPhysicalAdapter(const std::string& interface_name)
        : BaseAdapter(interface_name, "QueuedPhysicalAdapter") {}

    ~QueuedPhysicalAdapter() override {}

    std::unordered_map<int, Bus*> GetBuses() override {
        return GetKnownBusesSnapshot();
    }

    bool Ok() override {
        return true;
    }

    bool Accepted() const {
        return accepted_.load(std::memory_order_acquire);
    }

    bool PhysicallyCompleted() const {
        return physically_completed_.load(std::memory_order_acquire);
    }

protected:
    void Send(const MotorMessage&) override {
        accepted_.store(true, std::memory_order_release);
    }

    void Send(const MotorMessages&) override {
        accepted_.store(true, std::memory_order_release);
    }

private:
    std::atomic<bool> accepted_{false};
    std::atomic<bool> physically_completed_{false};
};

TEST(AdapterSoftSyncTests, FirstCommitEntersSoftSyncMode) {
    auto* adapter = MakeManagedTestAdapter("soft-sync-first-commit");
    auto* motor = adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(1.0F, 2.0F);
    ASSERT_TRUE(adapter->WaitForSentCount(1));
    adapter->ClearSentMessages();

    adapter->Commit();
    motor->SpdControl<0>(2.0F, 3.0F);
    EXPECT_FALSE(adapter->WaitForSentCount(1, std::chrono::milliseconds(20)));

    adapter->Commit();
    ASSERT_TRUE(adapter->WaitForSentCount(1));
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(BusSoftSyncTests, CommitOnlySubmitsCallingBus) {
    auto* adapter = MakeManagedTestAdapter("bus-soft-sync-isolated-commit");
    auto* first_bus = adapter->GetBus(0);
    auto* second_bus = adapter->GetBus(1);
    auto* first = first_bus->GetMotor(1, MotorModel::EC_A4310_P2);
    auto* second = second_bus->GetMotor(2, MotorModel::EC_A4310_P2);

    first_bus->SetSyncMode(true);
    second_bus->SetSyncMode(true);
    first->SpdControl<0>(1.0F, 2.0F);
    second->SpdControl<0>(3.0F, 4.0F);

    first_bus->Commit();

    const auto synchronized = adapter->GetSynchronizedBatches();
    ASSERT_EQ(synchronized.size(), 1U);
    ASSERT_EQ(synchronized.front().size(), 1U);
    EXPECT_EQ(synchronized.front().front().bus_idx, 0);
    EXPECT_EQ(synchronized.front().front().data.id, 1U);
    EXPECT_EQ(adapter->GetSentMessages().size(), 1U);

    second_bus->Commit();
    const auto all_synchronized = adapter->GetSynchronizedBatches();
    ASSERT_EQ(all_synchronized.size(), 2U);
    ASSERT_EQ(all_synchronized.back().size(), 1U);
    EXPECT_EQ(all_synchronized.back().front().bus_idx, 1);
    EXPECT_EQ(all_synchronized.back().front().data.id, 2U);
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(BusSoftSyncTests, LeavingModeReleasesOnlyCallingBusBacklog) {
    auto* adapter = MakeManagedTestAdapter("bus-soft-sync-isolated-release");
    auto* first_bus = adapter->GetBus(0);
    auto* second_bus = adapter->GetBus(1);
    auto* first = first_bus->GetMotor(1, MotorModel::EC_A4310_P2);
    auto* second = second_bus->GetMotor(2, MotorModel::EC_A4310_P2);

    first_bus->SetSyncMode(true);
    second_bus->SetSyncMode(true);
    first->SpdControl<0>(1.0F, 2.0F);
    second->SpdControl<0>(3.0F, 4.0F);
    EXPECT_TRUE(adapter->GetSentMessages().empty());

    first_bus->SetSyncMode(false);
    const auto released = adapter->GetSentMessages();
    ASSERT_EQ(released.size(), 1U);
    EXPECT_EQ(released.front().bus_idx, 0);
    EXPECT_TRUE(adapter->GetSynchronizedBatches().empty());

    second_bus->Commit();
    const auto synchronized = adapter->GetSynchronizedBatches();
    ASSERT_EQ(synchronized.size(), 1U);
    ASSERT_EQ(synchronized.front().size(), 1U);
    EXPECT_EQ(synchronized.front().front().bus_idx, 1);
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(BusSoftSyncTests, FirstCommitDoesNotRecoverDirectlySubmittedMessages) {
    auto* adapter = MakeManagedTestAdapter("bus-soft-sync-direct-first-commit");
    auto* bus = adapter->GetBus(0);
    auto* motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(1.0F, 2.0F);
    ASSERT_TRUE(adapter->WaitForSentCount(1));
    adapter->ClearSentMessages();

    bus->Commit();
    EXPECT_TRUE(adapter->GetSynchronizedBatches().empty());

    motor->SpdControl<0>(3.0F, 4.0F);
    EXPECT_TRUE(adapter->GetSentMessages().empty());
    bus->Commit();

    const auto synchronized = adapter->GetSynchronizedBatches();
    ASSERT_EQ(synchronized.size(), 1U);
    ASSERT_EQ(synchronized.front().size(), 1U);
    EXPECT_EQ(synchronized.front().front().data.id, 1U);
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(BusSoftSyncTests, AdapterModeIsInheritedByNewBuses) {
    auto* adapter = MakeManagedTestAdapter("bus-soft-sync-inherited-mode");
    adapter->SetSyncMode(true);

    auto* bus = adapter->GetBus(0);
    auto* motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);
    motor->SpdControl<0>(1.0F, 2.0F);
    EXPECT_TRUE(adapter->GetSentMessages().empty());

    bus->Commit();
    ASSERT_EQ(adapter->GetSynchronizedBatches().size(), 1U);

    adapter->SetSyncMode(false);
    adapter->ClearSentMessages();
    motor->SpdControl<0>(3.0F, 4.0F);
    ASSERT_TRUE(adapter->WaitForSentCount(1));
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(AdapterSoftSyncTests, CommitUsesSynchronizedTransportHookOnlyForCommittedBatch) {
    auto* adapter = MakeManagedTestAdapter("soft-sync-transport-hook");
    auto* motor = adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2);

    adapter->SetSyncMode(true);
    motor->SpdControl<0>(1.0F, 2.0F);
    adapter->Commit();

    const auto synchronized_batches = adapter->GetSynchronizedBatches();
    ASSERT_EQ(synchronized_batches.size(), 1U);
    ASSERT_EQ(synchronized_batches.front().size(), 1U);

    adapter->SetSyncMode(false);
    motor->SpdControl<0>(3.0F, 4.0F);
    ASSERT_TRUE(adapter->WaitForSentCount(2U));
    EXPECT_EQ(adapter->GetSynchronizedBatches().size(), 1U);

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(AdapterSoftSyncTests, ExplicitModeRetainsAndLeavingModeReleasesBacklog) {
    auto* adapter = MakeManagedTestAdapter("soft-sync-explicit");
    auto* motor = adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2);

    adapter->SetSyncMode(true);
    motor->SpdControl<0>(1.0F, 2.0F);
    motor->SpdControl<0>(2.0F, 3.0F);
    EXPECT_FALSE(adapter->WaitForSentCount(1, std::chrono::milliseconds(20)));

    adapter->SetSyncMode(false);
    ASSERT_TRUE(adapter->WaitForSentCount(2));
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(AdapterSoftSyncTests, CommitSubmitsAllCurrentDevicesAsOneBatch) {
    auto* adapter = MakeManagedTestAdapter("soft-sync-batch");
    auto* first = adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2);
    auto* second = adapter->GetBus(1)->GetMotor(2, MotorModel::EC_A4310_P2);

    adapter->SetSyncMode(true);
    first->SpdControl<0>(1.0F, 2.0F);
    first->SpdControl<0>(2.0F, 3.0F);
    second->SpdControl<0>(3.0F, 4.0F);
    adapter->Commit();

    ASSERT_TRUE(adapter->WaitForSentCount(3));
    const auto batches = adapter->GetSentBatches();
    ASSERT_EQ(batches.size(), 1U);
    EXPECT_EQ(batches.front().size(), 3U);
    EXPECT_EQ(batches.front()[0].bus_idx, 0);
    EXPECT_EQ(batches.front()[1].bus_idx, 0);
    EXPECT_EQ(batches.front()[2].bus_idx, 1);
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(AdapterSoftSyncTests, EmptyCommitIsHarmless) {
    auto* adapter = MakeManagedTestAdapter("soft-sync-empty");

    adapter->Commit();

    EXPECT_TRUE(adapter->GetSentMessages().empty());
    EXPECT_TRUE(adapter->GetSentBatches().empty());
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(AdapterSoftSyncTests, MotorBatteryAndPmsUseRegistrationOrderInOneBatch) {
    auto* adapter = MakeManagedTestAdapter("soft-sync-device-types");
    auto* bus = adapter->GetBus(0);
    auto* motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);
    auto* battery = bus->GetBattery(2);
    auto* pms = bus->GetPms();
    adapter->SetSyncMode(true);

    motor->SpdControl<0>(1.0F, 2.0F);
    battery->ClearFault();
    pms->SendCommand(PmsCommand::EnableChannel1);
    adapter->Commit();

    const auto batches = adapter->GetSentBatches();
    ASSERT_EQ(batches.size(), 1U);
    ASSERT_EQ(batches.front().size(), 3U);
    EXPECT_EQ(batches.front()[0].data.id, 1U);
    EXPECT_EQ(batches.front()[1].data.id, 0x4F6U);
    EXPECT_EQ(batches.front()[2].data.id, 0x18F3FFF2U);
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(AdapterSoftSyncTests, OneDeviceRetainsNewestTenMessagesInFifoOrder) {
    auto* adapter = MakeManagedTestAdapter("soft-sync-overwrite");
    auto* battery = adapter->GetBus(0)->GetBattery(0);
    adapter->SetSyncMode(true);

    for (std::uint16_t value = 0; value < 12; ++value) {
        BatteryPassiveCommands commands{};
        commands.allow_shutdown = (value & (1U << 0U)) != 0U;
        commands.allow_discharge = (value & (1U << 1U)) != 0U;
        commands.parallel_discharge = (value & (1U << 2U)) != 0U;
        commands.force_shutdown = (value & (1U << 3U)) != 0U;
        commands.request_charging = (value & (1U << 4U)) != 0U;
        commands.fault_shutdown_broadcast = (value & (1U << 5U)) != 0U;
        commands.configure_fault_thresholds = (value & (1U << 6U)) != 0U;
        commands.clear_fault = (value & (1U << 7U)) != 0U;
        commands.factory_mode = (value & (1U << 8U)) != 0U;
        commands.debug = (value & (1U << 9U)) != 0U;
        battery->SendPassiveCommands(commands);
    }
    adapter->Commit();

    const auto sent = adapter->GetSentMessages();
    ASSERT_EQ(sent.size(), 10U);
    for (std::size_t index = 0; index < sent.size(); ++index) {
        const auto encoded = static_cast<std::uint16_t>(
            sent[index].data.data[0] |
            (static_cast<std::uint16_t>(sent[index].data.data[1]) << 8U));
        EXPECT_EQ(encoded, index + 2U);
    }
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(AdapterSoftSyncTests, ConcurrentCommitsSubmitIndependentBatches) {
    auto& manager = EncosDriverManager::Instance();
    auto* adapter = static_cast<BlockingBatchAdapter*>(
        manager.CreateAdapterWithFactory("soft-sync-concurrent-commit", []() {
            return new BlockingBatchAdapter("soft-sync-concurrent-commit");
        }));
    auto* first_bus = adapter->GetBus(0);
    auto* second_bus = adapter->GetBus(1);
    auto* first = first_bus->GetMotor(1, MotorModel::EC_A4310_P2);
    auto* second = second_bus->GetMotor(2, MotorModel::EC_A4310_P2);
    first_bus->SetSyncMode(true);
    second_bus->SetSyncMode(true);
    first->SpdControl<0>(1.0F, 2.0F);
    second->SpdControl<0>(2.0F, 3.0F);

    auto first_commit = std::async(std::launch::async, [first_bus]() {
        first_bus->Commit();
    });
    adapter->WaitForFirstSubmission();
    auto second_commit = std::async(std::launch::async, [second_bus]() {
        second_bus->Commit();
    });
    EXPECT_TRUE(BaseAdapterTestAccess::SubmitMutexIsLocked(adapter));

    adapter->ReleaseFirstSubmission();
    first_commit.get();
    second_commit.get();

    const auto batches = adapter->GetBatches();
    ASSERT_EQ(batches.size(), 2U);
    ASSERT_EQ(batches[0].size(), 1U);
    ASSERT_EQ(batches[1].size(), 1U);
    EXPECT_EQ(batches[0][0].data.id, 1U);
    EXPECT_EQ(batches[1][0].data.id, 2U);
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

TEST(AdapterSoftSyncTests, CommitDoesNotWaitForPhysicalCompletion) {
    auto& manager = EncosDriverManager::Instance();
    auto* adapter = static_cast<QueuedPhysicalAdapter*>(
        manager.CreateAdapterWithFactory("soft-sync-physical", []() {
            return new QueuedPhysicalAdapter("soft-sync-physical");
        }));
    auto* motor = adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2);
    adapter->SetSyncMode(true);
    motor->SpdControl<0>(1.0F, 2.0F);

    adapter->Commit();

    EXPECT_TRUE(adapter->Accepted());
    EXPECT_FALSE(adapter->PhysicallyCompleted());
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

TEST(AdapterSoftSyncTests, DeletingDeviceDiscardsItsQueuedPortSafely) {
    auto& manager = EncosDriverManager::Instance();
    auto* adapter = MakeManagedTestAdapter("soft-sync-device-delete");
    auto* motor = adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2);
    adapter->SetSyncMode(true);
    motor->SpdControl<0>(1.0F, 2.0F);

    ASSERT_TRUE(manager.DestroyMotor(motor));
    adapter->Commit();

    EXPECT_TRUE(adapter->GetSentMessages().empty());
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

TEST(AdapterSoftSyncTests, ResponseWaitingApiDoesNotImplicitlyCommit) {
    auto* adapter = MakeManagedFakeAdapter("soft-sync-no-implicit-response-commit");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    auto* motor = adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2);
    adapter->SetSyncMode(true);

    const auto result = motor->SpdControl<1>(1.0F, 2.0F);

    EXPECT_EQ(result.error, MotorError::NoResponse);
    EXPECT_TRUE(adapter->GetCommandRecords().empty());
    adapter->Commit();
    EXPECT_EQ(WaitForCommandRecords(*adapter, 1).size(), 1U);
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

class StartBarrier {
public:
    explicit StartBarrier(std::size_t participants) : participants_(participants) {}

    void ArriveAndWait() {
        std::unique_lock<std::mutex> lock(mutex_);
        ++arrived_;
        if (arrived_ == participants_) {
            released_ = true;
            condition_.notify_all();
            return;
        }
        condition_.wait(lock, [this]() {
            return released_;
        });
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::size_t participants_;
    std::size_t arrived_ = 0;
    bool released_ = false;
};

TEST(AdapterSoftSyncTests, SameBatterySerializesConcurrentCommandPublication) {
    auto* adapter = MakeManagedTestAdapter("soft-sync-concurrent-battery");
    auto* battery = adapter->GetBus(0)->GetBattery(0);
    adapter->SetSyncMode(true);

    constexpr std::size_t kProducerCount = 8U;
    for (std::size_t round = 0; round < 50U; ++round) {
        adapter->ClearSentMessages();
        StartBarrier barrier(kProducerCount);
        std::vector<std::thread> producers;
        producers.reserve(kProducerCount);
        for (std::size_t index = 0; index < kProducerCount; ++index) {
            producers.emplace_back([&, index] {
                BatteryPassiveCommands commands{};
                const auto value = static_cast<std::uint16_t>(index + 1U);
                commands.allow_shutdown = (value & (1U << 0U)) != 0U;
                commands.allow_discharge = (value & (1U << 1U)) != 0U;
                commands.parallel_discharge = (value & (1U << 2U)) != 0U;
                commands.force_shutdown = (value & (1U << 3U)) != 0U;
                barrier.ArriveAndWait();
                battery->SendPassiveCommands(commands);
            });
        }
        for (auto& producer : producers) {
            producer.join();
        }
        adapter->Commit();

        const auto sent = adapter->GetSentMessages();
        ASSERT_EQ(sent.size(), kProducerCount);
        std::vector<std::uint16_t> encoded;
        encoded.reserve(sent.size());
        for (const auto& message : sent) {
            encoded.push_back(static_cast<std::uint16_t>(
                message.data.data[0] | (static_cast<std::uint16_t>(message.data.data[1]) << 8U)));
        }
        std::sort(encoded.begin(), encoded.end());
        for (std::size_t index = 0; index < encoded.size(); ++index) {
            EXPECT_EQ(encoded[index], index + 1U);
        }
    }

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(AdapterSoftSyncTests, SamePmsSerializesConcurrentCommandPublication) {
    auto* adapter = MakeManagedTestAdapter("soft-sync-concurrent-pms");
    auto* pms = adapter->GetBus(0)->GetPms();
    adapter->SetSyncMode(true);

    constexpr std::size_t kProducerCount = 6U;
    for (std::size_t round = 0; round < 50U; ++round) {
        adapter->ClearSentMessages();
        StartBarrier barrier(kProducerCount);
        std::vector<std::thread> producers;
        producers.reserve(kProducerCount);
        for (std::size_t index = 0; index < kProducerCount; ++index) {
            producers.emplace_back([&, index] {
                barrier.ArriveAndWait();
                pms->SendCommand(
                    static_cast<PmsCommand>(static_cast<std::uint16_t>(index + 1U) << 8U));
            });
        }
        for (auto& producer : producers) {
            producer.join();
        }
        adapter->Commit();

        const auto sent = adapter->GetSentMessages();
        ASSERT_EQ(sent.size(), kProducerCount);
        std::vector<std::uint8_t> encoded;
        encoded.reserve(sent.size());
        for (const auto& message : sent) {
            encoded.push_back(message.data.data[1]);
        }
        std::sort(encoded.begin(), encoded.end());
        for (std::size_t index = 0; index < encoded.size(); ++index) {
            EXPECT_EQ(encoded[index], index + 1U);
        }
    }

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(AdapterReceiveLifetimeTests, RawCallbackCannotDeleteOwningAdapterOrAnyOfItsBuses) {
    auto* adapter = MakeManagedTestAdapter("raw-context-delete");
    auto* first_bus = adapter->GetBus(1);
    auto* second_bus = adapter->GetBus(2);
    bool adapter_rejected = false;
    bool first_bus_rejected = false;
    bool second_bus_rejected = false;
    adapter->SetRawMessageCallbackForTests([&](const MotorMessages&) {
        adapter_rejected = !EncosDriverManager::Instance().DestroyAdapter(adapter);
        first_bus_rejected = !EncosDriverManager::Instance().DestroyBus(first_bus);
        second_bus_rejected = !EncosDriverManager::Instance().DestroyBus(second_bus);
    });

    MotorPackMsg first{};
    first.id = 0x601;
    MotorPackMsg second{};
    second.id = 0x602;
    adapter->SimulateOnMessage({MotorMessage{2, second}, MotorMessage{1, first}});

    EXPECT_TRUE(adapter_rejected);
    EXPECT_TRUE(first_bus_rejected);
    EXPECT_TRUE(second_bus_rejected);
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(AdapterReceiveLifetimeTests, ExternalAdapterDeletionWaitsForRawCallbackCompletion) {
    auto* adapter = MakeManagedTestAdapter("raw-context-drain");
    (void) adapter->GetBus(1);
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
    bool destroy_at_receive_drain = false;
    std::atomic<bool> destroyed{false};
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::SetDeletionHook(manager, [&](EncosDriverManager::DeletionStage stage) {
        if (stage != EncosDriverManager::DeletionStage::BeforeAdapterReceiveDrain) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex);
        destroy_at_receive_drain = true;
        condition.notify_all();
    });
    adapter->SetRawMessageCallbackForTests([&](const MotorMessages&) {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        condition.notify_all();
        condition.wait(lock, [&] {
            return release;
        });
    });

    std::thread receive([&] {
        adapter->SimulateOnMessage({MotorMessage{1, MotorPackMsg{}}});
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] {
            return entered;
        });
    }
    std::thread destroy([&] {
        destroyed.store(manager.DestroyAdapter(adapter));
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] {
            return destroy_at_receive_drain;
        });
    }
    EXPECT_FALSE(destroyed.load());
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_all();
    receive.join();
    destroy.join();
    EXPECT_TRUE(destroyed.load());
    DriverManagerTestAccess::SetDeletionHook(manager, {});
}

void WriteU16Le(uint8_t* target, uint16_t value) {
    target[0] = static_cast<uint8_t>(value & 0xFFu);
    target[1] = static_cast<uint8_t>(value >> 8u);
}

void WriteBitsLe(uint8_t* target, uint8_t start_bit, uint8_t bit_len, uint32_t value) {
    for (uint8_t bit = 0; bit < bit_len; ++bit) {
        if ((value & (1u << bit)) == 0) {
            continue;
        }
        const uint8_t absolute_bit = static_cast<uint8_t>(start_bit + bit);
        target[absolute_bit / 8u] =
            static_cast<uint8_t>(target[absolute_bit / 8u] | (1u << (absolute_bit % 8u)));
    }
}

MotorMessage MakeImuMessage(uint32_t id, std::initializer_list<uint8_t> bytes) {
    MotorMessage message{};
    message.bus_idx = 0;
    message.data.id = id;
    message.data.frame_flags = kCanFrameFlagEff;
    message.data.len = static_cast<uint8_t>(bytes.size());
    std::copy(bytes.begin(), bytes.end(), message.data.data);
    return message;
}

MotorMessage MakePmsMessage(uint32_t id, std::initializer_list<uint8_t> bytes) {
    MotorMessage message{};
    message.bus_idx = 0;
    message.data.id = id;
    message.data.frame_flags = kCanFrameFlagEff;
    message.data.len = static_cast<uint8_t>(bytes.size());
    std::copy(bytes.begin(), bytes.end(), message.data.data);
    return message;
}

}  // namespace

TEST(FakeAdapterBaseTests, InjectedFeedbackUpdatesAdapterStatusCache) {
    auto* adapter = MakeManagedFakeAdapter("fake-base");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    ASSERT_NE(adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2), nullptr);

    MotorStatus status{};
    status.error = MotorError::NoError;
    status.position = 0.5f;
    status.speed = 1.25f;
    status.current = 2.0f;
    status.motor_temperature = 25.0f;
    status.mos_temperature = 30.0f;

    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));

    const auto cached = adapter->GetMotorStatus(0, 1, 0);
    ASSERT_TRUE(cached.has_value());
    EXPECT_NEAR(cached->position, status.position, kDecodedAngleTolerance);
    EXPECT_NEAR(cached->speed, status.speed, kDecodedAngleTolerance);
    EXPECT_NEAR(cached->current, status.current, kDecodedFloatTolerance);
}

TEST(FakeAdapterBaseTests, FiltersFeedbackSpikeBeforeCachingAndCallingStatusCallback) {
    auto* adapter = MakeManagedFakeAdapter("fake-median-filter");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    ASSERT_NE(adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2), nullptr);
    adapter->SetStatusMedianFilterWindowSize(3);
    adapter->SetStatusLimitFilterMaxDeltas(5.0f, 5.0f, 20.0f,
                                           std::numeric_limits<float>::infinity(),
                                           std::numeric_limits<float>::infinity());

    MotorStatus callback_status{};
    adapter->SetOnStatus(0, 1, [&](const MotorStatus& status) {
        callback_status = status;
    });

    MotorStatus first{};
    first.error = MotorError::NoError;
    first.position = 1.0f;
    first.speed = 2.0f;
    first.current = 3.0f;
    first.motor_temperature = 25.0f;
    first.mos_temperature = 30.0f;

    MotorStatus second = first;
    second.position = 2.0f;
    second.speed = 3.0f;
    second.current = 4.0f;
    second.motor_temperature = 26.0f;
    second.mos_temperature = 31.0f;

    MotorStatus spike = second;
    spike.position = 10.0f;
    spike.speed = 12.0f;
    spike.current = 14.0f;
    spike.motor_temperature = 60.0f;
    spike.mos_temperature = 70.0f;

    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, first, 1));
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, second, 1));
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, spike, 1));

    const auto cached = adapter->GetMotorStatus(0, 1, 0);
    ASSERT_TRUE(cached.has_value());
    EXPECT_NEAR(cached->position, second.position, kDecodedAngleTolerance);
    EXPECT_NEAR(cached->speed, second.speed, kDecodedAngleTolerance);
    EXPECT_NEAR(cached->current, second.current, kDecodedFloatTolerance);
    EXPECT_NEAR(cached->motor_temperature, second.motor_temperature, kDecodedFloatTolerance);
    EXPECT_NEAR(cached->mos_temperature, second.mos_temperature, kDecodedFloatTolerance);
    EXPECT_NEAR(callback_status.position, cached->position, kDecodedAngleTolerance);
    EXPECT_NEAR(callback_status.speed, cached->speed, kDecodedAngleTolerance);
    EXPECT_NEAR(callback_status.current, cached->current, kDecodedFloatTolerance);
    EXPECT_NEAR(callback_status.motor_temperature, cached->motor_temperature,
                kDecodedFloatTolerance);
    EXPECT_NEAR(callback_status.mos_temperature, cached->mos_temperature, kDecodedFloatTolerance);
}

TEST(FakeAdapterBaseTests, KeepsFieldsAbsentFromFeedbackAsNan) {
    auto* adapter = MakeManagedFakeAdapter("fake-median-filter-partial-feedback");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    ASSERT_NE(adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2), nullptr);

    MotorStatus first{};
    first.error = MotorError::NoError;
    first.position = 1.0f;
    first.speed = 2.0f;
    first.current = 3.0f;
    first.motor_temperature = 25.0f;
    first.mos_temperature = 30.0f;

    MotorStatus callback_status{};
    adapter->SetOnStatus(0, 1, [&](const MotorStatus& status) {
        callback_status = status;
    });
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, first, 1));
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, first, 2));

    const auto cached = adapter->GetMotorStatus(0, 1, 0);
    ASSERT_TRUE(cached.has_value());
    EXPECT_TRUE(std::isnan(cached->speed));
    EXPECT_TRUE(std::isnan(cached->mos_temperature));
    EXPECT_TRUE(std::isnan(callback_status.speed));
    EXPECT_TRUE(std::isnan(callback_status.mos_temperature));
}

TEST(FakeAdapterBaseTests, PreservesSpeedFilterHistoryWhenStatusExpires) {
    auto* adapter = MakeManagedFakeAdapter("fake-median-filter-expiry");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    ASSERT_NE(adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2), nullptr);
    adapter->SetMaxStatusLifeCycle(1);
    adapter->SetStatusMedianFilterWindowSize(3);
    adapter->SetStatusLimitFilterMaxDeltas(5.0f, 5.0f, 20.0f,
                                           std::numeric_limits<float>::infinity(),
                                           std::numeric_limits<float>::infinity());

    MotorStatus old_status{};
    old_status.error = MotorError::NoError;
    old_status.position = 10.0f;
    old_status.speed = 10.0f;
    old_status.current = 10.0f;
    old_status.motor_temperature = 40.0f;
    old_status.mos_temperature = 45.0f;

    MotorStatus new_status = old_status;
    new_status.position = 1.0f;
    new_status.speed = -15.0f;
    new_status.current = 1.0f;
    new_status.motor_temperature = 20.0f;
    new_status.mos_temperature = 25.0f;

    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, old_status, 1));
    ASSERT_TRUE(adapter->GetMotorStatus(0, 1, 1).has_value());
    EXPECT_FALSE(adapter->GetMotorStatus(0, 1, 0).has_value());

    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, new_status, 1));
    const auto cached = adapter->GetMotorStatus(0, 1, 0);
    ASSERT_TRUE(cached.has_value());
    EXPECT_NEAR(cached->position, new_status.position, kDecodedAngleTolerance);
    EXPECT_NEAR(cached->speed, old_status.speed, kDecodedAngleTolerance);
    EXPECT_NEAR(cached->current, new_status.current, kDecodedFloatTolerance);
    EXPECT_NEAR(cached->motor_temperature, new_status.motor_temperature, kDecodedFloatTolerance);
    EXPECT_NEAR(cached->mos_temperature, new_status.mos_temperature, kDecodedFloatTolerance);
}

TEST(FakeAdapterBaseTests, RejectsOverLimitFeedbackWithoutUsingItAsTheNextReference) {
    auto* adapter = MakeManagedFakeAdapter("fake-limit-filter");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    ASSERT_NE(adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2), nullptr);
    adapter->SetStatusMedianFilterWindowSize(3);
    adapter->SetStatusLimitFilterMaxDeltas(5.0f, 5.0f, 20.0f,
                                           std::numeric_limits<float>::infinity(),
                                           std::numeric_limits<float>::infinity());

    MotorStatus first{};
    first.error = MotorError::NoError;
    first.position = 0.0f;
    first.speed = 0.0f;
    first.current = 0.0f;
    first.motor_temperature = 25.0f;
    first.mos_temperature = 30.0f;

    MotorStatus accepted = first;
    accepted.position = 1.0f;
    accepted.speed = 1.0f;
    accepted.current = 1.0f;

    MotorStatus rejected = accepted;
    rejected.position = 8.0f;
    rejected.speed = 8.0f;
    rejected.current = 22.0f;

    MotorStatus next = accepted;
    next.position = 7.0f;
    next.speed = 7.0f;
    next.current = 30.0f;

    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, first, 1));
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, accepted, 1));
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, rejected, 1));
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, next, 1));

    const auto cached = adapter->GetMotorStatus(0, 1, 0);
    ASSERT_TRUE(cached.has_value());
    EXPECT_NEAR(cached->position, accepted.position, kDecodedAngleTolerance);
    EXPECT_NEAR(cached->speed, accepted.speed, kDecodedAngleTolerance);
    EXPECT_NEAR(cached->current, accepted.current, kDecodedFloatTolerance);
}

TEST(FakeAdapterBaseTests, DisablesStatusFiltersByDefault) {
    auto* adapter = MakeManagedFakeAdapter("fake-status-filters-disabled");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    ASSERT_NE(adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2), nullptr);

    MotorStatus normal{};
    normal.error = MotorError::NoError;
    normal.position = 1.0f;
    normal.speed = 2.0f;
    normal.current = 3.0f;
    normal.motor_temperature = 25.0f;
    normal.mos_temperature = 30.0f;

    MotorStatus spike = normal;
    spike.position = 10.0f;
    spike.speed = 12.0f;
    spike.current = 30.0f;
    spike.motor_temperature = 40.0f;
    spike.mos_temperature = 45.0f;

    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, normal, 1));
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, spike, 1));

    const auto cached = adapter->GetMotorStatus(0, 1, 0);
    ASSERT_TRUE(cached.has_value());
    EXPECT_NEAR(cached->position, spike.position, kDecodedAngleTolerance);
    EXPECT_NEAR(cached->speed, spike.speed, kDecodedAngleTolerance);
    EXPECT_NEAR(cached->current, spike.current, kDecodedFloatTolerance);
    EXPECT_NEAR(cached->motor_temperature, spike.motor_temperature, kDecodedFloatTolerance);
    EXPECT_NEAR(cached->mos_temperature, spike.mos_temperature, kDecodedFloatTolerance);
}

TEST(FakeAdapterBaseTests, EnablesMedianAndLimitFiltersIndependently) {
    auto* median_adapter = MakeManagedFakeAdapter("fake-median-only");
    median_adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    ASSERT_NE(median_adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2), nullptr);
    median_adapter->SetStatusMedianFilterWindowSize(3);

    auto* limit_adapter = MakeManagedFakeAdapter("fake-limit-only");
    limit_adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    ASSERT_NE(limit_adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2), nullptr);
    limit_adapter->SetStatusLimitFilterMaxDeltas(5.0f, 5.0f, 20.0f, 5.0f, 5.0f);

    MotorStatus first{};
    first.error = MotorError::NoError;
    first.position = 0.0f;
    first.speed = 0.0f;
    first.current = 0.0f;
    first.motor_temperature = 25.0f;
    first.mos_temperature = 30.0f;
    MotorStatus second = first;
    second.position = 1.0f;
    second.speed = 1.0f;
    second.current = 1.0f;
    MotorStatus spike = second;
    spike.position = 8.0f;
    spike.speed = 8.0f;
    spike.current = 22.0f;
    spike.motor_temperature = 40.0f;
    spike.mos_temperature = 45.0f;

    for (auto* adapter : {median_adapter, limit_adapter}) {
        adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, first, 1));
        adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, second, 1));
        adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, spike, 1));
    }

    const auto median_status = median_adapter->GetMotorStatus(0, 1, 0);
    ASSERT_TRUE(median_status.has_value());
    EXPECT_NEAR(median_status->position, second.position, kDecodedAngleTolerance);
    EXPECT_NEAR(median_status->speed, second.speed, kDecodedAngleTolerance);
    EXPECT_NEAR(median_status->current, second.current, kDecodedFloatTolerance);
    EXPECT_NEAR(median_status->motor_temperature, second.motor_temperature, kDecodedFloatTolerance);
    EXPECT_NEAR(median_status->mos_temperature, second.mos_temperature, kDecodedFloatTolerance);

    const auto limit_status = limit_adapter->GetMotorStatus(0, 1, 0);
    ASSERT_TRUE(limit_status.has_value());
    EXPECT_NEAR(limit_status->position, second.position, kDecodedAngleTolerance);
    EXPECT_NEAR(limit_status->speed, second.speed, kDecodedAngleTolerance);
    EXPECT_NEAR(limit_status->current, second.current, kDecodedFloatTolerance);
    EXPECT_NEAR(limit_status->motor_temperature, second.motor_temperature, kDecodedFloatTolerance);
    EXPECT_NEAR(limit_status->mos_temperature, second.mos_temperature, kDecodedFloatTolerance);
}

TEST(FakeAdapterBaseTests, CanDisableMedianAndIndividualLimitFilters) {
    auto* adapter = MakeManagedFakeAdapter("fake-disable-status-filters");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    ASSERT_NE(adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2), nullptr);
    adapter->SetStatusMedianFilterWindowSize(3);
    adapter->SetStatusLimitFilterMaxDeltas(
        5.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());

    MotorStatus first{};
    first.error = MotorError::NoError;
    first.position = 0.0f;
    first.speed = 0.0f;
    first.current = 0.0f;
    first.motor_temperature = 25.0f;
    first.mos_temperature = 30.0f;
    MotorStatus second = first;
    second.position = 1.0f;
    second.current = 1.0f;
    MotorStatus third = second;
    third.position = 2.0f;
    third.current = 30.0f;

    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, first, 1));
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, second, 1));
    adapter->SetStatusMedianFilterWindowSize(0);
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, third, 1));

    const auto before_position_spike = adapter->GetMotorStatus(0, 1, 0);
    ASSERT_TRUE(before_position_spike.has_value());
    EXPECT_NEAR(before_position_spike->position, third.position, kDecodedAngleTolerance);
    EXPECT_NEAR(before_position_spike->current, third.current, kDecodedFloatTolerance);

    MotorStatus position_spike = third;
    position_spike.position = 8.0f;
    position_spike.current = 25.0f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, position_spike, 1));

    const auto cached = adapter->GetMotorStatus(0, 1, 0);
    ASSERT_TRUE(cached.has_value());
    EXPECT_NEAR(cached->position, third.position, kDecodedAngleTolerance);
    EXPECT_NEAR(cached->current, position_spike.current, kDecodedFloatTolerance);
}

TEST(FakeAdapterBaseTests, UpdatingLimitFilterKeepsMedianSamples) {
    auto* adapter = MakeManagedFakeAdapter("fake-limit-reconfigure-history");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    ASSERT_NE(adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2), nullptr);
    adapter->SetStatusMedianFilterWindowSize(3);
    adapter->SetStatusLimitFilterMaxDeltas(
        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity());

    MotorStatus status{};
    status.error = MotorError::NoError;
    status.position = 1.0f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));
    status.position = 2.0f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));
    adapter->SetStatusLimitFilterMaxDeltas(
        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity());
    status.position = 0.0f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));

    const auto cached = adapter->GetMotorStatus(0, 1, 0);
    ASSERT_TRUE(cached.has_value());
    EXPECT_NEAR(cached->position, 1.0f, kDecodedAngleTolerance);
}

TEST(FakeAdapterBaseTests, UpdatingLifeCycleKeepsMedianSamples) {
    auto* adapter = MakeManagedFakeAdapter("fake-lifecycle-reconfigure-history");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    ASSERT_NE(adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2), nullptr);
    adapter->SetStatusMedianFilterWindowSize(3);

    MotorStatus status{};
    status.error = MotorError::NoError;
    status.position = 1.0f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));
    status.position = 2.0f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));
    adapter->SetMaxStatusLifeCycle(10);
    status.position = 0.0f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));

    const auto cached = adapter->GetMotorStatus(0, 1, 0);
    ASSERT_TRUE(cached.has_value());
    EXPECT_NEAR(cached->position, 1.0f, kDecodedAngleTolerance);
}

TEST(FakeAdapterBaseTests, UpdatingMedianFilterKeepsLimitReference) {
    auto* adapter = MakeManagedFakeAdapter("fake-median-reconfigure-limit-reference");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    ASSERT_NE(adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2), nullptr);
    adapter->SetStatusLimitFilterMaxDeltas(
        1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());

    MotorStatus status{};
    status.error = MotorError::NoError;
    status.position = 1.0f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));
    adapter->SetStatusMedianFilterWindowSize(3);
    status.position = 10.0f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status, 1));

    const auto cached = adapter->GetMotorStatus(0, 1, 0);
    ASSERT_TRUE(cached.has_value());
    EXPECT_NEAR(cached->position, 1.0f, kDecodedAngleTolerance);
}

TEST(FakeAdapterBaseTests, ManualModeRecordsCommandsWithoutAutoReply) {
    auto* adapter = MakeManagedFakeAdapter("fake-manual");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    adapter->SetReplyMode(FakeReplyMode::Manual);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    const auto feedback = motor->SpdControl<1>(1.0f, 2.0f);
    EXPECT_EQ(feedback.error, MotorError::NoResponse);

    ASSERT_EQ(adapter->GetCommandRecords().size(), 1);
    const auto& payload = LastPayloadAs<FakeSpdControlPayload>(*adapter);
    EXPECT_NEAR(payload.speed, 1.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload.current, 2.0f, kDecodedFloatTolerance);
    EXPECT_EQ(payload.feedback_type, 1);
}

TEST(BusExternalDeviceTests, GetBatteryMarksBusButAllowsMotorAccess) {
    auto* adapter = MakeManagedTestAdapter();
    auto bus = adapter->GetBus(0);

    const auto battery = bus->GetBattery(0);
    ASSERT_NE(battery, nullptr);
    EXPECT_TRUE(bus->HasExternalDevice());

    const auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    EXPECT_EQ(bus->SelectMotor(1), motor);
    EXPECT_EQ(bus->GetMotors().size(), 1u);

    const auto discovered = bus->ScanMotors();
    EXPECT_TRUE(discovered.empty());
    EXPECT_TRUE(adapter->GetSentMessages().empty());
}

TEST(BusExternalDeviceTests, GetImuMarksBusButAllowsExplicitMotorAccess) {
    auto* adapter = MakeManagedTestAdapter();
    auto bus = adapter->GetBus(0);

    const auto imu = bus->GetImu(0);
    ASSERT_NE(imu, nullptr);
    EXPECT_TRUE(bus->HasExternalDevice());
    EXPECT_EQ(bus->GetImu(0), imu);

    const auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    EXPECT_EQ(bus->SelectMotor(1), motor);
    EXPECT_EQ(bus->GetMotors().size(), 1u);

    const auto discovered = bus->ScanMotors();
    EXPECT_TRUE(discovered.empty());
    EXPECT_TRUE(adapter->GetSentMessages().empty());
}

TEST(BusExternalDeviceTests, GetPmsCachesWrapperAndMarksBus) {
    auto* adapter = MakeManagedTestAdapter();
    auto bus = adapter->GetBus(0);

    const auto pms = bus->GetPms();
    ASSERT_NE(pms, nullptr);
    EXPECT_EQ(bus->GetPms(), pms);
    EXPECT_TRUE(bus->HasExternalDevice());

    const auto discovered = bus->ScanMotors();
    EXPECT_TRUE(discovered.empty());
    EXPECT_TRUE(adapter->GetSentMessages().empty());
}

TEST(PmsTests, AggregatesCompleteStatusAndCallsBackAfterAllFramesUpdate) {
    auto* adapter = MakeManagedTestAdapter();
    auto pms = adapter->GetBus(0)->GetPms();

    std::atomic<int> callback_count{0};
    PmsStatus callback_status{};
    pms->SetOnStatus([&](const PmsStatus& status) {
        callback_status = status;
        callback_count.fetch_add(1);
    });

    adapter->SimulateOnMessage(
        {MakePmsMessage(0x18F0FFF2, {0x25, 68, 0x34, 0x12, 0x2E, 0xFB, 0x37, 0x02})});
    EXPECT_EQ(callback_count.load(), 0);
    EXPECT_FALSE(pms->GetStatus().has_value());

    adapter->SimulateOnMessage(
        {MakePmsMessage(0x18F1FFF2, {0x64, 0x00, 0x38, 0xFF, 0x2C, 0x01, 0x70, 0xFE})});
    EXPECT_EQ(callback_count.load(), 0);
    EXPECT_FALSE(pms->GetStatus().has_value());

    adapter->SimulateOnMessage(
        {MakePmsMessage(0x18F2FFF2, {0xF4, 0x01, 0xA8, 0xFD, 0xBC, 0x02, 0xE0, 0xFC})});

    const auto status = pms->GetStatus();
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(callback_count.load(), 1);
    EXPECT_TRUE(callback_status.v48_channel_enabled[0]);
    EXPECT_TRUE(status->v48_channel_enabled[0]);
    EXPECT_FALSE(status->v48_channel_enabled[1]);
    EXPECT_TRUE(status->v48_channel_enabled[2]);
    EXPECT_FALSE(status->v48_channel_enabled[3]);
    EXPECT_FALSE(status->v48_channel_enabled[4]);
    EXPECT_TRUE(status->v48_channel_enabled[5]);
    EXPECT_EQ(status->battery_soc, 68);
    EXPECT_FLOAT_EQ(status->battery_voltage, 46.60f);
    EXPECT_FLOAT_EQ(status->battery_current, -12.34f);
    EXPECT_FLOAT_EQ(status->v5_current, 5.67f);
    EXPECT_FLOAT_EQ(status->v48_currents[0], 1.0f);
    EXPECT_FLOAT_EQ(status->v48_currents[1], -2.0f);
    EXPECT_FLOAT_EQ(status->v48_currents[2], 3.0f);
    EXPECT_FLOAT_EQ(status->v48_currents[3], -4.0f);
    EXPECT_FLOAT_EQ(status->v48_currents[4], 5.0f);
    EXPECT_FLOAT_EQ(status->v48_currents[5], -6.0f);
    EXPECT_FLOAT_EQ(status->v19_currents[0], 7.0f);
    EXPECT_FLOAT_EQ(status->v19_currents[1], -8.0f);

    adapter->SimulateOnMessage(
        {MakePmsMessage(0x18F0FFF2, {0x01, 50, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00})});
    EXPECT_EQ(callback_count.load(), 1);
    adapter->SimulateOnMessage({
        MakePmsMessage(0x18F1FFF2, {0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}),
        MakePmsMessage(0x18F2FFF2, {0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}),
    });
    EXPECT_EQ(callback_count.load(), 2);
}

TEST(PmsTests, IgnoresShortFramesAndInvalidatesWholeStatusWhenOneFrameExpires) {
    auto* adapter = MakeManagedTestAdapter();
    auto pms = adapter->GetBus(0)->GetPms();

    std::atomic<int> callback_count{0};
    pms->SetOnStatus([&](const PmsStatus&) {
        callback_count.fetch_add(1);
    });

    adapter->SimulateOnMessage({MakePmsMessage(0x18F0FFF2, {0x01, 50})});
    EXPECT_FALSE(pms->GetStatus().has_value());
    EXPECT_EQ(callback_count.load(), 0);

    adapter->SimulateOnMessage({
        MakePmsMessage(0x18F0FFF2, {0x01, 50, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00}),
        MakePmsMessage(0x18F1FFF2, {0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}),
        MakePmsMessage(0x18F2FFF2, {0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}),
    });
    ASSERT_TRUE(pms->GetStatus().has_value());
    ASSERT_EQ(callback_count.load(), 1);

    DeviceStatusTestAccess::ExpireAllPmsFrames(pms);
    adapter->SimulateOnMessage(
        {MakePmsMessage(0x18F1FFF2, {0xC8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}),
         MakePmsMessage(0x18F2FFF2, {0xC8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})});

    const auto status = pms->GetStatus();
    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(callback_count.load(), 1);
}

TEST(PmsTests, SendCommandEncodesExtendedChannelControlFrame) {
    auto* adapter = MakeManagedTestAdapter();
    auto pms = adapter->GetBus(0)->GetPms();

    pms->SendCommand(PmsCommand::EnableChannel1 | PmsCommand::DisableChannel2);

    ASSERT_TRUE(adapter->WaitForSentCount(1));
    const auto sent = adapter->GetSentMessages();
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].bus_idx, 0);
    EXPECT_EQ(sent[0].data.id, 0x18F3FFF2u);
    EXPECT_EQ(sent[0].data.frame_flags, kCanFrameFlagEff);
    EXPECT_EQ(sent[0].data.len, 8u);
    EXPECT_EQ(sent[0].data.data[0], 0x02);
    EXPECT_EQ(sent[0].data.data[1], 0x01);
    for (size_t index = 2; index < 8; ++index) {
        EXPECT_EQ(sent[0].data.data[index], 0);
    }
}

TEST(PmsTests, SendCommandRejectsConflictingChannelActions) {
    auto* adapter = MakeManagedTestAdapter();
    auto pms = adapter->GetBus(0)->GetPms();

    EXPECT_THROW(pms->SendCommand(PmsCommand::EnableChannel3 | PmsCommand::DisableChannel3),
                 std::invalid_argument);
    EXPECT_TRUE(adapter->GetSentMessages().empty());
}

TEST(BusExternalDeviceTests, DetectExternalDeviceConsumesFreshTrafficAndSetsFlag) {
    auto* adapter = MakeManagedTestAdapter();
    auto bus = adapter->GetBus(0);

    MotorMessage parameter_reply{};
    parameter_reply.bus_idx = 0;
    parameter_reply.data.id = 0x123;
    parameter_reply.data.len = 6;
    parameter_reply.data.data[0] = static_cast<uint8_t>(0x05 << 5);
    parameter_reply.data.data[1] = static_cast<uint8_t>(MotorParameter::Position);

    std::thread injector([adapter, parameter_reply]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        adapter->SimulateOnMessage({parameter_reply});
    });

    EXPECT_TRUE(bus->DetectExternalDevice());
    EXPECT_TRUE(bus->HasExternalDevice());

    injector.join();
}

TEST(BusExternalDeviceTests, DetectExternalDeviceKeepsFlagForRegisteredExternalDevice) {
    auto* adapter = MakeManagedTestAdapter();
    auto bus = adapter->GetBus(0);

    const auto battery = bus->GetBattery(0);
    ASSERT_NE(battery, nullptr);
    EXPECT_TRUE(bus->HasExternalDevice());

    EXPECT_TRUE(bus->DetectExternalDevice());
    EXPECT_TRUE(bus->HasExternalDevice());
}

TEST(BusExternalDeviceTests, ExternalDeviceCreationConflictRollsBackBusFlag) {
    auto* adapter = MakeManagedTestAdapter();
    auto* bus = adapter->GetBus(0);
    ASSERT_NE(bus->GetMotor(0x3F4, MotorModel::EC_A4310_P2), nullptr);
    ASSERT_FALSE(bus->HasExternalDevice());

    EXPECT_THROW((void) bus->GetBattery(0), std::runtime_error);
    EXPECT_FALSE(bus->HasExternalDevice());
}

TEST(BusExternalDeviceTests, ScanMotorsAbortsBeforeSendingDiscoveryQueries) {
    auto* adapter = MakeManagedTestAdapter();
    auto bus = adapter->GetBus(0);

    MotorMessage parameter_reply{};
    parameter_reply.bus_idx = 0;
    parameter_reply.data.id = 0x456;
    parameter_reply.data.len = 6;
    parameter_reply.data.data[0] = static_cast<uint8_t>(0x05 << 5);
    parameter_reply.data.data[1] = static_cast<uint8_t>(MotorParameter::Position);

    std::thread injector([adapter, parameter_reply]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        adapter->SimulateOnMessage({parameter_reply});
    });

    const auto discovered = bus->ScanMotors();
    EXPECT_TRUE(discovered.empty());
    EXPECT_TRUE(bus->HasExternalDevice());
    EXPECT_TRUE(adapter->GetSentMessages().empty());

    injector.join();
}

TEST(BusExternalDeviceTests, ScanMotorsKeepsCurrentBusRepliesWhenOtherBusHasTraffic) {
    auto* adapter = MakeManagedFakeAdapter("fake-multi-bus-external");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    auto bus = adapter->GetBus(0);

    int external_traffic_injections = 0;
    adapter->SetDecodedCommandObserver(
        [adapter, &external_traffic_injections](const FakeCommandRecord& record) {
            if (record.bus_idx != 0 || record.motor_idx != 2 ||
                record.kind != FakeCommandKind::GetParameter) {
                return;
            }

            const auto* payload = std::get_if<FakeGetParameterPayload>(&record.payload);
            if (payload == nullptr || payload->parameter != MotorParameter::Position) {
                return;
            }

            ++external_traffic_injections;
            for (int i = 0; i < 300; ++i) {
                MotorMessage external{};
                external.bus_idx = 7;
                external.data.id = static_cast<uint32_t>(0x3F4 + (i % 4));
                external.data.len = 8;
                external.data.data[0] = static_cast<uint8_t>(i);
                adapter->InjectMessage(external);
            }
        });

    const auto discovered = bus->ScanMotors();

    EXPECT_EQ(external_traffic_injections, 1);
    ASSERT_EQ(discovered.size(), 1u);
    EXPECT_NE(discovered.find(1), discovered.end());
}

TEST(BatteryTests, DirectCallbacksDecodeLittleEndianFrames) {
    auto* adapter = MakeManagedTestAdapter();
    auto bus = adapter->GetBus(0);
    auto battery = bus->GetBattery(0);

    std::atomic<int> callback_count{0};
    BatteryStatus callback_status{};
    battery->SetOnStatus([&](const BatteryStatus& status) {
        callback_status = status;
        callback_count.fetch_add(1);
    });

    MotorMessage state{};
    state.bus_idx = 0;
    state.data.id = 0x3F4;
    state.data.len = 8;
    state.data.data[0] = 0x01;
    state.data.data[1] = 55;
    state.data.data[2] = 0x5E;
    state.data.data[3] = 0x01;
    state.data.data[4] = 0xD0;
    state.data.data[5] = 0x07;
    state.data.data[6] = 0xB8;
    state.data.data[7] = 0x0B;

    MotorMessage temp{};
    temp.bus_idx = 0;
    temp.data.id = 0x2F4;
    temp.data.len = 8;
    temp.data.data[0] = 0x19;
    temp.data.data[1] = 0x00;
    temp.data.data[2] = 0x1E;
    temp.data.data[3] = 0x00;
    temp.data.data[4] = 0xE8;
    temp.data.data[5] = 0x03;
    temp.data.data[6] = 0xF4;
    temp.data.data[7] = 0x01;

    MotorMessage error{};
    error.bus_idx = 0;
    error.data.id = 0x0F4;
    error.data.len = 2;
    error.data.data[0] = 0x01;
    error.data.data[1] = 0x04;

    adapter->SimulateOnMessage({state, temp, error});

    const auto status = battery->GetStatus();
    ASSERT_TRUE(status.state.has_value());
    ASSERT_TRUE(status.temp.has_value());
    EXPECT_EQ(callback_count.load(), 3);
    EXPECT_TRUE(callback_status.state.has_value());
    EXPECT_TRUE(callback_status.temp.has_value());
    EXPECT_TRUE(status.state->is_master);
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
}

TEST(BatteryTests, DecodesActiveCommandsFrame) {
    auto* adapter = MakeManagedTestAdapter();
    auto bus = adapter->GetBus(0);
    auto battery = bus->GetBattery(0);

    MotorMessage active{};
    active.bus_idx = 0;
    active.data.id = 0x1F4;
    active.data.len = 2;
    active.data.data[0] = 0x3F;

    adapter->SimulateOnMessage({active});

    const auto status = battery->GetStatus();
    ASSERT_TRUE(status.active_commands.has_value());
    EXPECT_TRUE(status.active_commands->shutdown_request);
    EXPECT_TRUE(status.active_commands->discharge_request);
    EXPECT_TRUE(status.active_commands->force_shutdown_broadcast);
    EXPECT_TRUE(status.active_commands->allow_charging);
    EXPECT_TRUE(status.active_commands->fault_shutdown_broadcast);
    EXPECT_TRUE(status.active_commands->mos_status);
}

TEST(BatteryTests, ClearFaultSendsPassiveCommandFrame) {
    auto* adapter = MakeManagedTestAdapter();
    auto bus = adapter->GetBus(0);
    auto battery = bus->GetBattery(0);

    battery->ClearFault();

    ASSERT_TRUE(adapter->WaitForSentCount(1));
    const auto sent = adapter->GetSentMessages();
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].bus_idx, 0);
    EXPECT_EQ(sent[0].data.id, 0x4F4u);
    EXPECT_EQ(sent[0].data.len, 2u);
    EXPECT_EQ(sent[0].data.data[0], 0x80);
    EXPECT_EQ(sent[0].data.data[1], 0x00);
}

TEST(ImuTests, UpdatePassCoalescesCallbackAndDecodesYis130Frames) {
    auto* adapter = MakeManagedTestAdapter();
    auto bus = adapter->GetBus(0);
    auto imu = bus->GetImu(0);

    std::atomic<int> callback_count{0};
    ImuStatus callback_status{};
    imu->SetOnStatus([&](const ImuStatus& status) {
        callback_status = status;
        callback_count.fetch_add(1);
    });

    uint8_t angular_data[8]{};
    WriteBitsLe(angular_data, 0, 20, 512000);
    WriteBitsLe(angular_data, 20, 20, 512128);
    WriteBitsLe(angular_data, 40, 20, 511872);

    uint8_t quaternion_data[8]{};
    WriteU16Le(quaternion_data, 32766);
    WriteU16Le(quaternion_data + 2, 0);
    WriteU16Le(quaternion_data + 4, 65535);
    WriteU16Le(quaternion_data + 6, 32768);

    adapter->SimulateOnMessage({
        MakeImuMessage(0x0CF02D59, {0x00, 0x7D, 0x64, 0x7D, 0x9C, 0x7C}),
        MakeImuMessage(0x0CF02A59,
                       {angular_data[0], angular_data[1], angular_data[2], angular_data[3],
                        angular_data[4], angular_data[5], angular_data[6], angular_data[7]}),
        MakeImuMessage(0x0CF02959, {0x00, 0x7D, 0x80, 0x7D, 0x80, 0x7C}),
        MakeImuMessage(0x0CF03059, {quaternion_data[0], quaternion_data[1], quaternion_data[2],
                                    quaternion_data[3], quaternion_data[4], quaternion_data[5],
                                    quaternion_data[6], quaternion_data[7]}),
    });

    const auto status = imu->GetStatus();
    ASSERT_TRUE(status.acceleration.has_value());
    ASSERT_TRUE(status.angular_velocity.has_value());
    ASSERT_TRUE(status.euler_angle.has_value());
    ASSERT_TRUE(status.quaternion.has_value());
    EXPECT_EQ(callback_count.load(), 4);
    EXPECT_TRUE(callback_status.acceleration.has_value());
    EXPECT_TRUE(callback_status.angular_velocity.has_value());
    EXPECT_TRUE(callback_status.euler_angle.has_value());
    EXPECT_TRUE(callback_status.quaternion.has_value());
    // 浮点解码在不同平台（尤其 ARM FMA）下可能存在微小偏差，统一使用 EXPECT_NEAR
    EXPECT_NEAR(status.acceleration->x, 0.0f, 1e-5f);
    EXPECT_NEAR(status.acceleration->y, 1.0f, 1e-5f);
    EXPECT_NEAR(status.acceleration->z, -1.0f, 1e-5f);
    EXPECT_NEAR(status.angular_velocity->x, 0.0f, 1e-5f);
    EXPECT_NEAR(status.angular_velocity->y, 1.0f, 1e-5f);
    EXPECT_NEAR(status.angular_velocity->z, -1.0f, 1e-5f);
    EXPECT_NEAR(status.euler_angle->pitch, 0.0f, 1e-5f);
    EXPECT_NEAR(status.euler_angle->roll, 1.0f, 1e-5f);
    EXPECT_NEAR(status.euler_angle->heading, -1.0f, 1e-5f);
    EXPECT_NEAR(status.quaternion->qw, -0.0000144243f, 0.000001f);
    EXPECT_NEAR(status.quaternion->qx, -1.0f, 1e-5f);
    EXPECT_NEAR(status.quaternion->qy, 1.0000627f, 0.000001f);
    EXPECT_NEAR(status.quaternion->qz, 0.0000466108f, 0.000001f);
}

TEST(ImuTests, IgnoresShortFramesAndExpiresStaleGroups) {
    auto* adapter = MakeManagedTestAdapter();
    auto bus = adapter->GetBus(0);
    auto imu = bus->GetImu(0);

    std::atomic<int> callback_count{0};
    imu->SetOnStatus([&](const ImuStatus&) {
        callback_count.fetch_add(1);
    });

    adapter->SimulateOnMessage({MakeImuMessage(0x0CF02D59, {0x00, 0x7D})});

    auto status = imu->GetStatus();
    EXPECT_FALSE(status.acceleration.has_value());
    EXPECT_EQ(callback_count.load(), 0);

    adapter->SimulateOnMessage({MakeImuMessage(0x0CF02D59, {0x00, 0x7D, 0x64, 0x7D, 0x9C, 0x7C})});
    status = imu->GetStatus();
    ASSERT_TRUE(status.acceleration.has_value());
    EXPECT_EQ(callback_count.load(), 1);

    DeviceStatusTestAccess::ExpireImuAcceleration(imu);
    status = imu->GetStatus();
    EXPECT_FALSE(status.acceleration.has_value());
}

TEST(ImuTests, ExposesNativeAndJsUpdateIntervals) {
    EXPECT_EQ(Imu::NativeUpdateInterval(), std::chrono::milliseconds(5));
    EXPECT_EQ(Imu::JsUpdateInterval(), std::chrono::milliseconds(20));
}

}  // namespace encos
