#include <atomic>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

#include "platform/sync.h"

namespace encos::platform {
namespace {

TEST(PlatformSyncTest, LinuxMutexesUsePriorityInheritance) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    EXPECT_TRUE(kMutexUsesPriorityInheritance);
    Mutex mutex;
    RecursiveMutex recursive_mutex;
    EXPECT_TRUE(mutex.UsesPriorityInheritance());
    EXPECT_TRUE(recursive_mutex.UsesPriorityInheritance());
#else
    EXPECT_FALSE(kMutexUsesPriorityInheritance);
#endif
}

TEST(PlatformSyncTest, MutexSerializesCompetingThreads) {
    Mutex mutex;
    std::atomic<bool> entered{false};
    std::mutex start_mutex;
    std::condition_variable start_condition;
    bool contender_started = false;
    mutex.lock();
    std::thread contender([&] {
        {
            std::lock_guard<std::mutex> lock(start_mutex);
            contender_started = true;
        }
        start_condition.notify_one();
        LockGuard<Mutex> lock(mutex);
        entered.store(true, std::memory_order_release);
    });
    {
        std::unique_lock<std::mutex> lock(start_mutex);
        start_condition.wait(lock, [&] {
            return contender_started;
        });
    }
    EXPECT_FALSE(entered.load(std::memory_order_acquire));
    mutex.unlock();
    contender.join();
    EXPECT_TRUE(entered.load(std::memory_order_acquire));
}

TEST(PlatformSyncTest, RecursiveMutexAllowsNestedLocking) {
    RecursiveMutex mutex;
    mutex.lock();
    EXPECT_TRUE(mutex.try_lock());
    mutex.unlock();
    mutex.unlock();
}

}  // namespace
}  // namespace encos::platform
