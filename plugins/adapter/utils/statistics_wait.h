// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace encos {

/** @brief 统计采样间隔等待；运行标志的修改与等待使用同一互斥量，停止不会丢失唤醒。 */
class StatisticsWait {
public:
    void Start(std::atomic<bool>& running) {
        std::lock_guard<std::mutex> lock(mutex_);
        running.store(true);
    }

    void Stop(std::atomic<bool>& running) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running.store(false);
        }

        changed_.notify_all();
    }

    bool Wait(std::atomic<bool>& running) {
        std::unique_lock<std::mutex> lock(mutex_);
        return !changed_.wait_for(lock, std::chrono::milliseconds(500), [&running] {
            return !running.load();
        });
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
};

}  // namespace encos
