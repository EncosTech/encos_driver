#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "motor/motor.h"
#include "motor/motor_log_transport.h"
#include "platform/sync.h"

namespace encos {

struct Motor::Impl {
    std::atomic<uint16_t> idx{0};
    std::atomic<uint8_t> frame_flags{0};
    MotorPVTRanges ranges{};
    std::atomic<float> current_range{0.0f};
    Bus* bus = nullptr;
    std::function<void(const MotorPackMsg&)> writer;
    mutable platform::RecursiveMutex motor_mutex;
    /** 只保护日志 writer、恢复状态和重试计数。 */
    mutable platform::Mutex log_mutex;
    struct Waiter {
        std::function<bool(const MotorPackMsg&)> checker;
        std::shared_ptr<void> content;
        std::function<void(const MotorPackMsg&, void*)> writer;
        platform::Mutex mutex;
        std::condition_variable_any cv;
        std::atomic<bool> completed{false};
        std::atomic<bool> cancelled{false};
    };
    platform::Mutex waiter_mutex;
    std::vector<std::shared_ptr<Waiter>> waiters;
    bool rejecting_waiters = false;
    std::atomic<std::size_t> active_waiter_operations{0};
    std::condition_variable_any waiter_operations_drained;
    std::function<void()> test_on_waiter_registered;
    std::function<void()> test_on_waiter_checker;
    std::function<void()> test_on_waiter_writer;
    std::function<void()> test_on_waiters_cancelled;
    platform::Mutex status_mutex;
    std::optional<MotorStatus> status;
    int status_life_cycle = std::numeric_limits<int>::max();
    int max_status_life_cycle = std::numeric_limits<int>::max();
    struct ContinuousStatusSamples {
        std::deque<float> position;
        std::deque<float> speed;
        std::deque<float> current;
        std::deque<float> motor_temperature;
        std::deque<float> mos_temperature;
    } status_filter_samples;
    MotorStatus last_accepted_status;
    std::size_t status_median_filter_window_size = 0;
    MotorStatus status_limit_filter_max_deltas;
    LoggerPtr logger_;
    /** 用户回调与日志 I/O 使用独立锁域，避免回调配置被慢日志写入阻塞。 */
    mutable platform::Mutex callback_mutex;
    std::function<void(const MotorStatus&)> user_on_status;
    std::shared_ptr<detail::MotorLogSession> log_session;
    std::string log_base_name;
    int log_retry_budget = 3;
    int log_success_count = 0;
    bool log_recovering = false;
};

}  // namespace encos
