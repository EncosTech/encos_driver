#include <chrono>
#include <condition_variable>
#include <future>
#include <gtest/gtest.h>
#include <mutex>
#include <set>
#include <thread>

#include "adapter/base_adapter.h"
#include "base_adapter_test_access.h"
#include "bus/bus.h"
#include "bus/bus_impl.h"
#include "driver_manager_test_access.h"
#include "encos/driver_manager.h"
#include "motor/motor.h"
#include "test_adapter.h"
#include "wait_observer.h"

namespace encos {

class BusPortTestAccess {
public:
    static MotorMessages Drain(Bus* bus) {
        if (bus == nullptr) {
            return {};
        }
        platform::LockGuard<platform::Mutex> lock(bus->impl_->consumer_mutex);
        return bus->DrainUnknownMessagesLocked();
    }

    static std::unordered_map<int, Motor*> Scan(Bus* bus) {
        if (bus == nullptr) {
            return {};
        }
        return bus->ScanMotorsForTesting();
    }

    static bool ConsumerMutexIsLocked(Bus* bus) {
        if (bus->impl_->consumer_mutex.try_lock()) {
            bus->impl_->consumer_mutex.unlock();
            return false;
        }
        return true;
    }
};

namespace {

class BlockingScanAdapter final : public BaseAdapter {
public:
    BlockingScanAdapter() : BaseAdapter("blocking-scan", "BlockingScanAdapter", LogLevel::Info) {}

    std::unordered_map<int, Bus*> GetBuses() override {
        return GetKnownBusesSnapshot();
    }

    bool Ok() override {
        return true;
    }

    void WaitForFirstDiscoverySend() {
        std::unique_lock<std::mutex> lock(mutex_);
        ASSERT_TRUE(condition_.wait_for(lock, std::chrono::seconds(3), [this] {
            return first_discovery_send_;
        }));
    }

    void WaitForBusOneDiscoverySend() {
        std::unique_lock<std::mutex> lock(mutex_);
        ASSERT_TRUE(condition_.wait_for(lock, std::chrono::seconds(3), [this] {
            return bus_one_discovery_send_;
        }));
    }

    int BusZeroDiscoverySendCount() {
        std::lock_guard<std::mutex> lock(mutex_);
        return bus_zero_discovery_send_count_;
    }

    bool BusOneDiscoverySendObserved() {
        std::lock_guard<std::mutex> lock(mutex_);
        return bus_one_discovery_send_;
    }

    void ReleaseDiscoverySend() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            release_discovery_send_ = true;
        }
        condition_.notify_all();
    }

protected:
    void Send(const MotorMessage& message) override {
        if (message.data.len != 2 || message.data.data[0] != static_cast<uint8_t>(0x07 << 5)) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        if (message.bus_idx == 1) {
            bus_one_discovery_send_ = true;
            condition_.notify_all();
            return;
        }
        ++bus_zero_discovery_send_count_;
        first_discovery_send_ = true;
        condition_.notify_all();
        condition_.wait(lock, [this] {
            return release_discovery_send_;
        });
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool first_discovery_send_ = false;
    bool bus_one_discovery_send_ = false;
    bool release_discovery_send_ = false;
    int bus_zero_discovery_send_count_ = 0;
};

class DiscoveryReplyAdapter final : public BaseAdapter {
public:
    DiscoveryReplyAdapter()
        : BaseAdapter("discovery-replies", "DiscoveryReplyAdapter", LogLevel::Info) {}

    std::unordered_map<int, Bus*> GetBuses() override {
        return GetKnownBusesSnapshot();
    }

    bool Ok() override {
        return true;
    }

protected:
    void Send(const MotorMessage& message) override {
        if (message.data.len != 2 || message.data.data[0] != static_cast<uint8_t>(0x07 << 5)) {
            return;
        }
        if (sent_discovery_ && message.data.id == 7) {
            MotorMessage parameter_reply{};
            parameter_reply.bus_idx = message.bus_idx;
            parameter_reply.data.id = 7;
            parameter_reply.data.frame_flags = kCanFrameFlagEff;
            parameter_reply.data.len = 6;
            parameter_reply.data.data[0] = static_cast<uint8_t>(0x05 << 5);
            parameter_reply.data.data[1] = message.data.data[1];
            OnMessage({parameter_reply});
            return;
        }
        if (sent_discovery_) {
            return;
        }
        sent_discovery_ = true;
        MotorMessage valid{};
        valid.bus_idx = message.bus_idx;
        valid.data.id = 7;
        valid.data.frame_flags = kCanFrameFlagEff;
        valid.data.len = 6;
        valid.data.data[0] = static_cast<uint8_t>(0x05 << 5);
        valid.data.data[1] = static_cast<uint8_t>(MotorParameter::Position);
        MotorMessage invalid = valid;
        invalid.data.id = 8;
        invalid.data.len = 5;
        OnMessage({valid, valid, invalid});
    }

private:
    bool sent_discovery_ = false;
};

class ProtocolEchoDiscoveryAdapter final : public BaseAdapter {
public:
    ProtocolEchoDiscoveryAdapter()
        : BaseAdapter("protocol-echo-discovery", "ProtocolEchoDiscoveryAdapter", LogLevel::Info) {}

    std::unordered_map<int, Bus*> GetBuses() override {
        return GetKnownBusesSnapshot();
    }

    bool Ok() override {
        return true;
    }

    const std::set<int>& CanFdProbeIds() const {
        return can_fd_probe_ids_;
    }

protected:
    void Send(const MotorMessage& message) override {
        if (message.data.len != 2 || message.data.data[0] != static_cast<uint8_t>(0x07 << 5) ||
            message.data.id < 1 || message.data.id > 3) {
            return;
        }

        const bool uses_can_fd = CanFrameFlagsUseCanFd(message.data.frame_flags);
        MotorMessage reply{};
        reply.bus_idx = message.bus_idx;
        reply.data.id = message.data.id;
        reply.data.len = 6;
        reply.data.data[0] = static_cast<uint8_t>(0x05 << 5);
        reply.data.data[1] = message.data.data[1];
        if (!uses_can_fd) {
            if (message.data.id == 1) {
                reply.data.frame_flags = kCanFrameFlagFdMask;
            } else if (message.data.id == 2 || message.data.id == 3) {
                reply.data.frame_flags = 0;
            } else {
                return;
            }
        } else {
            can_fd_probe_ids_.insert(message.data.id);
            if (message.data.id == 1 || message.data.id == 3) {
                reply.data.frame_flags = kCanFrameFlagFdMask;
            } else if (message.data.id == 2) {
                reply.data.frame_flags = 0;
            } else {
                return;
            }
        }
        OnMessage({reply});
    }

private:
    std::set<int> can_fd_probe_ids_;
};

TEST(BusPortScanTests, DestroyBusWaitsForScanBatchAcceptance) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::Reset(manager);
    auto* adapter =
        static_cast<BlockingScanAdapter*>(manager.CreateAdapterWithFactory("blocking-scan", [] {
            return new BlockingScanAdapter();
        }));
    ASSERT_NE(adapter, nullptr);
    auto* bus = manager.CreateBus(adapter, 0);
    ASSERT_NE(bus, nullptr);
    auto scan = std::async(std::launch::async, [bus] {
        return BusPortTestAccess::Scan(bus);
    });
    adapter->WaitForFirstDiscoverySend();
    WaitObserver wait_observer;
    DriverManagerTestAccess::SetWaitHook(manager, [&] {
        wait_observer.Notify();
    });
    std::atomic<bool> destroy_completed{false};
    auto destroy = std::async(std::launch::async, [&] {
        const bool result = manager.DestroyBus(bus);
        destroy_completed.store(true, std::memory_order_release);
        return result;
    });
    wait_observer.WaitForCount(1U);
    EXPECT_FALSE(destroy_completed.load(std::memory_order_acquire));
    EXPECT_TRUE(BusPortTestAccess::ConsumerMutexIsLocked(bus));

    adapter->ReleaseDiscoverySend();
    EXPECT_TRUE(scan.get().empty());
    EXPECT_TRUE(destroy.get());
    DriverManagerTestAccess::SetWaitHook(manager, {});
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

TEST(BusPortScanTests, UnknownFramesStayInTheirOwningBusMailbox) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::Reset(manager);
    const std::string interface_name = "bus-port-isolation";
    auto* adapter = static_cast<TestAdapter*>(
        manager.CreateAdapterWithFactory(interface_name, [interface_name] {
            return new TestAdapter(interface_name);
        }));
    ASSERT_NE(adapter, nullptr);
    auto* first_bus = manager.CreateBus(adapter, 1);
    auto* second_bus = manager.CreateBus(adapter, 2);
    ASSERT_NE(first_bus, nullptr);
    ASSERT_NE(second_bus, nullptr);

    MotorMessage first{};
    first.bus_idx = 1;
    first.data.id = 0x401;
    first.data.len = 1;
    first.data.data[0] = 0xA1;
    MotorMessage second = first;
    second.bus_idx = 2;
    second.data.id = 0x402;
    second.data.data[0] = 0xB2;
    adapter->SimulateOnMessage({first, second});

    const auto first_messages = BusPortTestAccess::Drain(first_bus);
    const auto second_messages = BusPortTestAccess::Drain(second_bus);
    ASSERT_EQ(first_messages.size(), 1u);
    ASSERT_EQ(second_messages.size(), 1u);
    EXPECT_EQ(first_messages.front().data, first.data);
    EXPECT_EQ(second_messages.front().data, second.data);
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

TEST(BusPortScanTests, SameBusScansSerializeTheirMailboxConsumer) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::Reset(manager);
    auto* adapter =
        static_cast<BlockingScanAdapter*>(manager.CreateAdapterWithFactory("blocking-scan", [] {
            return new BlockingScanAdapter();
        }));
    auto* bus = manager.CreateBus(adapter, 0);
    auto first_scan = std::async(std::launch::async, [bus] {
        return BusPortTestAccess::Scan(bus);
    });
    adapter->WaitForFirstDiscoverySend();
    auto second_scan = std::async(std::launch::async, [bus] {
        return BusPortTestAccess::Scan(bus);
    });
    EXPECT_TRUE(BusPortTestAccess::ConsumerMutexIsLocked(bus));
    EXPECT_EQ(adapter->BusZeroDiscoverySendCount(), 1);
    adapter->ReleaseDiscoverySend();
    EXPECT_TRUE(first_scan.get().empty());
    EXPECT_TRUE(second_scan.get().empty());
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

TEST(BusPortScanTests, DifferentBusScansQueueConcurrentlyAndSubmitSerially) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::Reset(manager);
    auto* adapter =
        static_cast<BlockingScanAdapter*>(manager.CreateAdapterWithFactory("blocking-scan", [] {
            return new BlockingScanAdapter();
        }));
    auto* first_bus = manager.CreateBus(adapter, 0);
    auto* second_bus = manager.CreateBus(adapter, 1);
    auto first_scan = std::async(std::launch::async, [first_bus] {
        return BusPortTestAccess::Scan(first_bus);
    });
    adapter->WaitForFirstDiscoverySend();
    auto second_scan = std::async(std::launch::async, [second_bus] {
        return BusPortTestAccess::Scan(second_bus);
    });
    EXPECT_TRUE(BaseAdapterTestAccess::SubmitMutexIsLocked(adapter));
    EXPECT_FALSE(adapter->BusOneDiscoverySendObserved());
    adapter->ReleaseDiscoverySend();
    adapter->WaitForBusOneDiscoverySend();
    EXPECT_TRUE(first_scan.get().empty());
    EXPECT_TRUE(second_scan.get().empty());
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

TEST(BusPortScanTests, ScanClearsStaleFramesAndKeepsExistingNonResponders) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::Reset(manager);
    const std::string interface_name = "bus-port-stale";
    auto* adapter = static_cast<TestAdapter*>(
        manager.CreateAdapterWithFactory(interface_name, [interface_name] {
            return new TestAdapter(interface_name);
        }));
    auto* bus = manager.CreateBus(adapter, 0);
    auto* existing = manager.CreateMotor(bus, 42, MotorModel::EC_A4310_P2);
    ASSERT_NE(existing, nullptr);

    MotorMessage stale{};
    stale.bus_idx = 0;
    stale.data.id = 0x420;
    stale.data.len = 1;
    adapter->SimulateOnMessage({stale});
    const auto discovered = BusPortTestAccess::Scan(bus);

    ASSERT_EQ(discovered.size(), 1u);
    EXPECT_EQ(discovered.at(42), existing);
    EXPECT_TRUE(BusPortTestAccess::Drain(bus).empty());
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

TEST(BusPortScanTests, ScanAcceptsOneValidReplyAndRejectsInvalidOrDuplicateReplies) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::Reset(manager);
    auto* adapter = static_cast<DiscoveryReplyAdapter*>(
        manager.CreateAdapterWithFactory("discovery-replies", [] {
            return new DiscoveryReplyAdapter();
        }));
    ASSERT_NE(adapter, nullptr);
    auto* bus = manager.CreateBus(adapter, 0);
    ASSERT_NE(bus, nullptr);

    const auto discovered = BusPortTestAccess::Scan(bus);

    ASSERT_EQ(discovered.size(), 1u);
    ASSERT_NE(discovered.find(7), discovered.end());
    EXPECT_TRUE(discovered.at(7)->IsCanEffEnabled());
    EXPECT_EQ(manager.FindMotor(bus, 8), nullptr);
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

TEST(BusPortScanTests, ScanUsesCanFdRepliesAsHighestPriorityForCanDiscoveredIds) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::Reset(manager);
    auto* adapter = static_cast<ProtocolEchoDiscoveryAdapter*>(
        manager.CreateAdapterWithFactory("protocol-echo-discovery", [] {
            return new ProtocolEchoDiscoveryAdapter();
        }));
    ASSERT_NE(adapter, nullptr);
    auto* bus = manager.CreateBus(adapter, 0);
    ASSERT_NE(bus, nullptr);

    const auto discovered = BusPortTestAccess::Scan(bus);

    ASSERT_EQ(discovered.size(), 3u);
    EXPECT_TRUE(discovered.at(1)->IsCanFdEnabled());
    EXPECT_FALSE(discovered.at(2)->IsCanFdEnabled());
    EXPECT_TRUE(discovered.at(3)->IsCanFdEnabled());
    EXPECT_EQ(adapter->CanFdProbeIds(), (std::set<int>{1, 2, 3}));
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

TEST(BusPortScanTests, ScanCancelsWhenExistingDiscoveredMotorIsRetiring) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::Reset(manager);
    auto* adapter = static_cast<DiscoveryReplyAdapter*>(
        manager.CreateAdapterWithFactory("discovery-replies", [] {
            return new DiscoveryReplyAdapter();
        }));
    ASSERT_NE(adapter, nullptr);
    auto* bus = manager.CreateBus(adapter, 0);
    auto* existing = manager.CreateMotor(bus, 7, MotorModel::EC_A4310_P2);
    ASSERT_NE(existing, nullptr);

    std::mutex mutex;
    std::condition_variable condition;
    bool deleting = false;
    bool release_delete = false;
    DriverManagerTestAccess::SetDeletionHook(manager, [&](EncosDriverManager::DeletionStage stage) {
        if (stage != EncosDriverManager::DeletionStage::BeforeDeviceDestroy) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex);
        deleting = true;
        condition.notify_all();
        condition.wait(lock, [&] {
            return release_delete;
        });
    });
    auto destroy = std::async(std::launch::async, [&] {
        return manager.DestroyMotor(existing);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] {
            return deleting;
        });
    }
    EXPECT_TRUE(BusPortTestAccess::Scan(bus).empty());
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_delete = true;
    }
    condition.notify_all();
    EXPECT_TRUE(destroy.get());
    DriverManagerTestAccess::SetDeletionHook(manager, {});
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

TEST(BusPortScanTests, ScanConvertsParentRetirementDuringDiscoveryToCancellation) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::Reset(manager);
    auto* adapter = static_cast<DiscoveryReplyAdapter*>(
        manager.CreateAdapterWithFactory("discovery-replies", [] {
            return new DiscoveryReplyAdapter();
        }));
    ASSERT_NE(adapter, nullptr);
    auto* bus = manager.CreateBus(adapter, 0);

    std::mutex mutex;
    std::condition_variable condition;
    bool initializing = false;
    bool release_initializer = false;
    DriverManagerTestAccess::SetDeviceInitializerHook(manager, [&](void*) {
        std::unique_lock<std::mutex> lock(mutex);
        initializing = true;
        condition.notify_all();
        condition.wait(lock, [&] {
            return release_initializer;
        });
    });
    auto scan = std::async(std::launch::async, [bus] {
        return BusPortTestAccess::Scan(bus);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] {
            return initializing;
        });
    }
    WaitObserver wait_observer;
    DriverManagerTestAccess::SetWaitHook(manager, [&] {
        wait_observer.Notify();
    });
    std::atomic<bool> destroy_completed{false};
    auto destroy = std::async(std::launch::async, [&] {
        const bool result = manager.DestroyBus(bus);
        destroy_completed.store(true, std::memory_order_release);
        return result;
    });
    wait_observer.WaitForCount(1U);
    EXPECT_FALSE(destroy_completed.load(std::memory_order_acquire));
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_initializer = true;
    }
    condition.notify_all();
    EXPECT_TRUE(scan.get().empty());
    EXPECT_TRUE(destroy.get());
    DriverManagerTestAccess::SetWaitHook(manager, {});
    DriverManagerTestAccess::SetDeviceInitializerHook(manager, {});
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

}  // namespace
}  // namespace encos
