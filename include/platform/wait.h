#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

#ifdef __EMSCRIPTEN__
#include "platform/delay.h"
#endif

namespace encos::platform {

/**
 * @brief 等待谓词成立或超时
 *
 * 原生平台使用条件变量；单线程 Emscripten 平台通过 Asyncify 周期性让出事件循环，
 * 避免阻塞 WebSocket、定时器和删除取消回调。
 *
 * @param condition 原生平台使用的条件变量
 * @param lock 调用方持有的互斥锁
 * @param timeout 最大等待时间
 * @param predicate 完成或取消谓词；Emscripten 下必须由原子状态实现
 * @return 谓词是否在返回时成立
 */
template <typename ConditionVariable, typename MutexType, typename Predicate>
bool WaitForPredicate(ConditionVariable& condition, std::unique_lock<MutexType>& lock,
                      std::chrono::milliseconds timeout, Predicate predicate) {
#ifdef __EMSCRIPTEN__
    if (predicate()) {
        return true;
    }
    lock.unlock();
    try {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!predicate() && std::chrono::steady_clock::now() < deadline) {
            SleepFor(std::chrono::milliseconds(1));
        }
    } catch (...) {
        lock.lock();
        throw;
    }
    lock.lock();
    return predicate();
#else
    return condition.wait_for(lock, timeout, std::move(predicate));
#endif
}

/**
 * @brief 等待谓词成立
 * @param condition 原生平台使用的条件变量
 * @param lock 调用方持有的互斥锁
 * @param predicate 完成谓词；Emscripten 下必须由原子状态实现
 */
template <typename ConditionVariable, typename MutexType, typename Predicate>
void WaitForPredicate(ConditionVariable& condition, std::unique_lock<MutexType>& lock,
                      Predicate predicate) {
#ifdef __EMSCRIPTEN__
    if (predicate()) {
        return;
    }
    lock.unlock();
    try {
        while (!predicate()) {
            SleepFor(std::chrono::milliseconds(1));
        }
    } catch (...) {
        lock.lock();
        throw;
    }
    lock.lock();
#else
    condition.wait(lock, std::move(predicate));
#endif
}

}  // namespace encos::platform
