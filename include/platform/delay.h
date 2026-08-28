#pragma once

#include <chrono>
#include <thread>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace encos::platform {

template <typename Rep, typename Period>
inline void SleepFor(const std::chrono::duration<Rep, Period>& duration) {
#ifdef __EMSCRIPTEN__
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
    emscripten_sleep(static_cast<unsigned int>(ms.count() < 0 ? 0 : ms.count()));
#else
    std::this_thread::sleep_for(duration);
#endif
}

template <typename Clock, typename Duration>
inline void SleepUntil(const std::chrono::time_point<Clock, Duration>& time_point) {
#ifdef __EMSCRIPTEN__
    const auto now = Clock::now();
    if (time_point > now) {
        SleepFor(time_point - now);
    }
#else
    std::this_thread::sleep_until(time_point);
#endif
}

}  // namespace encos::platform
