// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <thread>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#ifdef _WIN32
// clang-format off
#include <windows.h>
// clang-format on
#endif

namespace encos::platform {

#ifdef _WIN32
namespace detail {

/** @brief 每个线程复用的高精度可等待定时器，用于精确 SleepUntil */
inline HANDLE HighResolutionTimer() {
    static thread_local HANDLE timer = []() -> HANDLE {
        HANDLE handle = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (handle == nullptr) {
            handle = CreateWaitableTimerW(nullptr, TRUE, nullptr);
        }
        return handle;
    }();
    return timer;
}

}  // namespace detail
#endif

/**
 * @brief 阻塞当前执行流指定时长
 * @tparam Rep 时长计数类型
 * @tparam Period 时长单位比例
 * @param duration 等待时长；Emscripten 下负值按零处理
 */
template <typename Rep, typename Period>
inline void SleepFor(const std::chrono::duration<Rep, Period>& duration) {
#ifdef __EMSCRIPTEN__
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
    emscripten_sleep(static_cast<unsigned int>(ms.count() < 0 ? 0 : ms.count()));
#else
    std::this_thread::sleep_for(duration);
#endif
}

/**
 * @brief 阻塞当前执行流直至指定时间点
 * @tparam Clock 时钟类型
 * @tparam Duration 时间点精度
 * @param time_point 目标时间点
 */
template <typename Clock, typename Duration>
inline void SleepUntil(const std::chrono::time_point<Clock, Duration>& time_point) {
#ifdef __EMSCRIPTEN__
    const auto now = Clock::now();
    if (time_point > now) {
        SleepFor(time_point - now);
    }
#elif defined(_WIN32)
    const auto remaining =
        std::chrono::duration_cast<std::chrono::nanoseconds>(time_point - Clock::now()).count();
    if (remaining <= 0) {
        return;
    }
    HANDLE timer = detail::HighResolutionTimer();
    LARGE_INTEGER due{};
    // 负值表示相对时间，单位 100ns。
    due.QuadPart = -static_cast<LONGLONG>(remaining / 100);
    if (due.QuadPart == 0) {
        due.QuadPart = -1;
    }
    if (timer != nullptr && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) != 0) {
        WaitForSingleObject(timer, INFINITE);
        return;
    }
    std::this_thread::sleep_until(time_point);
#else
    std::this_thread::sleep_until(time_point);
#endif
}

}  // namespace encos::platform
