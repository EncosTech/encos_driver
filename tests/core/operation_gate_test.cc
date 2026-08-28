#include "operation_gate.h"

#include <atomic>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

namespace encos {
namespace {

TEST(OperationRegistryTest, RetiredSidecarRemainsStableWithoutDereferencingObject) {
    OperationRegistry registry;
    auto* object = reinterpret_cast<void*>(0x12340);

    auto* gate = registry.Register(object, OperationKind::Motor);
    ASSERT_EQ(registry.TryEnter(object, OperationKind::Motor), gate);

    auto* retired = registry.Retire(object, OperationKind::Motor);
    EXPECT_EQ(retired, gate);
    EXPECT_EQ(registry.TryEnter(object, OperationKind::Motor), nullptr);

    gate->Leave();
    retired->WaitForDrain();
    registry.ReclaimRetired(object, OperationKind::Motor);
    EXPECT_EQ(registry.EntryCountForTests(), 0U);
}

TEST(OperationRegistryTest, AddressReusePublishesANewGeneration) {
    OperationRegistry registry;
    auto* object = reinterpret_cast<void*>(0x56780);

    auto* first = registry.Register(object, OperationKind::Bus);
    ASSERT_EQ(registry.Retire(object, OperationKind::Bus), first);
    first->WaitForDrain();
    registry.ReclaimRetired(object, OperationKind::Bus);

    auto* second = registry.Register(object, OperationKind::Bus);
    EXPECT_EQ(registry.TryEnter(object, OperationKind::Bus), second);
    EXPECT_EQ(registry.TryEnter(object, OperationKind::Motor), nullptr);
    second->Leave();
}

TEST(OperationRegistryTest, RepeatedChurnReclaimsEntriesAndKeepsLookupBounded) {
    OperationRegistry registry;
    auto* object = reinterpret_cast<void*>(0x9ABC0);

    for (std::size_t iteration = 0; iteration < 10000; ++iteration) {
        auto* gate = registry.Register(object, OperationKind::Motor);
        ASSERT_EQ(registry.Retire(object, OperationKind::Motor), gate);
        gate->WaitForDrain();
        registry.ReclaimRetired(object, OperationKind::Motor);
    }

    EXPECT_EQ(registry.EntryCountForTests(), 0U);
    EXPECT_EQ(registry.MaxProbeForTests(), 0U);
}

TEST(OperationRegistryTest, HazardCapacityExhaustionHasDistinctDiagnostic) {
    BasicOperationRegistry<1> registry;
    auto* object = reinterpret_cast<void*>(0xBEEF0);
    auto* gate = registry.Register(object, OperationKind::Motor);

    std::mutex mutex;
    std::condition_variable condition;
    bool first_entered = false;
    bool release_first = false;
    std::thread first([&] {
        const auto result = registry.TryEnterDetailed(object, OperationKind::Motor);
        ASSERT_EQ(result.failure, OperationEnterFailure::None);
        ASSERT_EQ(result.gate, gate);
        {
            std::unique_lock<std::mutex> lock(mutex);
            first_entered = true;
            condition.notify_all();
            condition.wait(lock, [&] {
                return release_first;
            });
        }
        result.gate->Leave();
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] {
            return first_entered;
        });
    }

    const auto exhausted = registry.TryEnterDetailed(object, OperationKind::Motor);
    EXPECT_EQ(exhausted.gate, nullptr);
    EXPECT_EQ(exhausted.failure, OperationEnterFailure::HazardCapacityExhausted);

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_first = true;
    }
    condition.notify_all();
    first.join();
}

TEST(OperationRegistryTest, ConcurrentRetireDrainAndAddressReusePublishNewGeneration) {
    OperationRegistry registry;
    auto* object = reinterpret_cast<void*>(0xCAFE0);
    auto* first_gate = registry.Register(object, OperationKind::Motor);
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};

    std::thread operation([&] {
        auto* gate = registry.TryEnter(object, OperationKind::Motor);
        ASSERT_EQ(gate, first_gate);
        entered.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        gate->Leave();
    });
    while (!entered.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    auto* retired = registry.Retire(object, OperationKind::Motor);
    ASSERT_EQ(retired, first_gate);
    EXPECT_EQ(registry.TryEnter(object, OperationKind::Motor), nullptr);
    release.store(true, std::memory_order_release);
    operation.join();
    retired->WaitForDrain();
    registry.ReclaimRetired(object, OperationKind::Motor);

    auto* second_gate = registry.Register(object, OperationKind::Motor);
    ASSERT_NE(second_gate, nullptr);
    auto* entered_second = registry.TryEnter(object, OperationKind::Motor);
    EXPECT_EQ(entered_second, second_gate);
    entered_second->Leave();
}

}  // namespace
}  // namespace encos
