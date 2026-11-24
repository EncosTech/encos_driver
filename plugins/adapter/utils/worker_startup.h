// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <exception>
#include <functional>
#include <future>
#include <thread>
#include <utility>

namespace encos::adapter_worker {

/** @brief 由真实工作线程完成初始化；调用方等待结果，失败时回收线程并保留异常类型。 */
inline void Start(std::thread& worker, std::function<void()> initialize,
                  std::function<void()> run) {
    std::promise<void> initialized;
    auto ready = initialized.get_future();

    worker = std::thread([initialize = std::move(initialize), run = std::move(run),
                          initialized = std::move(initialized)]() mutable {
        try {
            initialize();
        } catch (...) {
            initialized.set_exception(std::current_exception());
            return;
        }

        initialized.set_value();
        run();
    });

    try {
        ready.get();
    } catch (...) {
        worker.join();
        throw;
    }
}

}  // namespace encos::adapter_worker
