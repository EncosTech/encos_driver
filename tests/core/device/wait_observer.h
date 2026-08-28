#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <gtest/gtest.h>
#include <mutex>

namespace encos {

class WaitObserver {
public:
    void Notify() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++count_;
        }
        condition_.notify_all();
    }

    void WaitForCount(std::size_t expected) {
        std::unique_lock<std::mutex> lock(mutex_);
        ASSERT_TRUE(condition_.wait_for(lock, std::chrono::seconds(3), [&] {
            return count_ >= expected;
        }));
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::size_t count_ = 0;
};

}  // namespace encos
