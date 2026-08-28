#include "motor/motor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <thread>
#include <vector>

#include "bus/bus_impl.h"
#include "encos/driver_manager.h"
#include "motor/math_utils.h"
#include "motor/motor_impl.h"
#include "motor/pack_helper.h"
#include "platform/delay.h"
#include "platform/wait.h"

namespace encos {

namespace {

uint8_t WithCanFdFlag(uint8_t frame_flags, bool canfd) {
    frame_flags = SanitizeCanFrameFlags(frame_flags);
    if (canfd) {
        return static_cast<uint8_t>(frame_flags | kCanFrameFlagFdMask);
    }
    return static_cast<uint8_t>(frame_flags & static_cast<uint8_t>(~kCanFrameFlagFdMask));
}

float MedianStatusValue(const std::deque<float>& values) {
    std::vector<float> sorted_values(values.begin(), values.end());
    std::sort(sorted_values.begin(), sorted_values.end());
    return sorted_values[sorted_values.size() / 2];
}

bool IsMedianStatusFilterEnabled(std::size_t window_size) {
    return window_size > 1;
}

bool IsLimitStatusFilterEnabled(float max_delta) {
    return std::isfinite(max_delta) && max_delta >= 0.0f;
}

float FilterStatusValue(float value, float& last_accepted_value, std::deque<float>& samples,
                        float max_delta, std::size_t window_size) {
    if (!std::isfinite(value)) {
        return value;
    }
    if (IsLimitStatusFilterEnabled(max_delta) && std::isfinite(last_accepted_value) &&
        std::fabs(value - last_accepted_value) > max_delta) {
        if (IsMedianStatusFilterEnabled(window_size) && !samples.empty()) {
            return MedianStatusValue(samples);
        }
        return last_accepted_value;
    }
    last_accepted_value = value;
    if (!IsMedianStatusFilterEnabled(window_size)) {
        return value;
    }
    samples.push_back(value);
    if (samples.size() > window_size) {
        samples.pop_front();
    }
    return MedianStatusValue(samples);
}

}  // namespace

Motor::Motor(Bus* bus, uint16_t motor_idx, MotorModel model, LoggerPtr logger,
             std::function<void(const MotorPackMsg&)> writer, uint8_t frame_flags) {
    impl_ = std::make_unique<Impl>();
    impl_->idx.store(motor_idx, std::memory_order_relaxed);
    impl_->frame_flags.store(SanitizeCanFrameFlags(frame_flags), std::memory_order_relaxed);
    impl_->bus = bus;
    impl_->writer = std::move(writer);
    impl_->logger_ = std::move(logger);
    impl_->ranges = GetMotorModelRanges(model);
    impl_->current_range = impl_->ranges.current.max;
}

Motor::Motor(Bus* bus, uint16_t motor_idx, MotorModel model, LoggerPtr logger,
             std::function<void(const MotorPackMsg&)> writer, uint8_t frame_flags, bool canfd)
    : Motor(bus, motor_idx, model, std::move(logger), std::move(writer),
            WithCanFdFlag(frame_flags, canfd)) {}

Motor::Motor(Bus* bus, uint16_t motor_idx, MotorPVTRanges ranges, LoggerPtr logger,
             std::function<void(const MotorPackMsg&)> writer, uint8_t frame_flags) {
    impl_ = std::make_unique<Impl>();
    impl_->idx.store(motor_idx, std::memory_order_relaxed);
    impl_->frame_flags.store(SanitizeCanFrameFlags(frame_flags), std::memory_order_relaxed);
    impl_->ranges = ranges;
    impl_->current_range = ranges.current.max;
    impl_->bus = bus;
    impl_->writer = std::move(writer);
    impl_->logger_ = std::move(logger);
}

Motor::Motor(Bus* bus, uint16_t motor_idx, MotorPVTRanges ranges, LoggerPtr logger,
             std::function<void(const MotorPackMsg&)> writer, uint8_t frame_flags, bool canfd)
    : Motor(bus, motor_idx, ranges, std::move(logger), std::move(writer),
            WithCanFdFlag(frame_flags, canfd)) {}

Motor::Motor(Bus* bus, uint16_t motor_idx, LoggerPtr logger,
             std::function<void(const MotorPackMsg&)> writer, uint8_t frame_flags) {
    impl_ = std::make_unique<Impl>();
    impl_->idx.store(motor_idx, std::memory_order_relaxed);
    impl_->frame_flags.store(SanitizeCanFrameFlags(frame_flags), std::memory_order_relaxed);
    impl_->bus = bus;
    impl_->writer = std::move(writer);
    impl_->logger_ = std::move(logger);
}

Motor::Motor(Bus* bus, uint16_t motor_idx, LoggerPtr logger,
             std::function<void(const MotorPackMsg&)> writer, uint8_t frame_flags, bool canfd)
    : Motor(bus, motor_idx, std::move(logger), std::move(writer),
            WithCanFdFlag(frame_flags, canfd)) {}

bool Motor::SendAndWaitErased(const MotorPackMsg& message,
                              std::function<bool(const MotorPackMsg&)> checker,
                              std::shared_ptr<void> content,
                              std::function<void(const MotorPackMsg&, void*)> writer,
                              std::chrono::milliseconds timeout) {
    auto waiter = std::make_shared<Impl::Waiter>();
    waiter->checker = std::move(checker);
    waiter->content = std::move(content);
    waiter->writer = std::move(writer);

    {
        platform::LockGuard<platform::Mutex> lock(impl_->waiter_mutex);
        if (impl_->rejecting_waiters) {
            return false;
        }
        impl_->active_waiter_operations.fetch_add(1, std::memory_order_relaxed);
        impl_->waiters.push_back(waiter);
    }

    const auto unregister = [this, &waiter]() {
        {
            platform::LockGuard<platform::Mutex> lock(waiter->mutex);
            waiter->cancelled.store(true, std::memory_order_release);
        }
        platform::LockGuard<platform::Mutex> lock(impl_->waiter_mutex);
        auto& waiters = impl_->waiters;
        waiters.erase(std::remove(waiters.begin(), waiters.end(), waiter), waiters.end());
        if (impl_->active_waiter_operations.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            impl_->waiter_operations_drained.notify_all();
        }
    };

    std::function<void()> on_registered;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->waiter_mutex);
        on_registered = impl_->test_on_waiter_registered;
    }
    try {
        if (on_registered) {
            on_registered();
        }
    } catch (...) {
        unregister();
        throw;
    }

    try {
        SendMessageLocked(message);
    } catch (...) {
        unregister();
        throw;
    }

    {
        platform::UniqueLock<platform::Mutex> lock(waiter->mutex);
        platform::WaitForPredicate(waiter->cv, lock, timeout, [&waiter] {
            return waiter->completed.load(std::memory_order_acquire) ||
                   waiter->cancelled.load(std::memory_order_acquire);
        });
        const bool completed = waiter->completed.load(std::memory_order_acquire);
        lock.unlock();
        unregister();
        return completed;
    }
}
std::optional<MotorPackMsg> Motor::SendAndWait(const MotorPackMsg& message,
                                               std::function<bool(const MotorPackMsg&)> checker,
                                               std::chrono::milliseconds timeout) {
    return SendAndWaitTyped<MotorPackMsg>(
        message, std::move(checker),
        [](const MotorPackMsg& received, MotorPackMsg& result) {
            result = received;
        },
        timeout);
}
bool Motor::SendParameterAndWait(const MotorPackMsg& message, uint8_t parameter_id,
                                 std::vector<uint8_t> expected_payload) {
    return SendAndWaitTyped<bool>(
               message,
               [parameter_id,
                expected_payload = std::move(expected_payload)](const MotorPackMsg& pack) {
                   return packet_has_payload(pack, 0, 3 + expected_payload.size()) &&
                          pack.data[0] == 0xff && pack.data[1] == 0xfe &&
                          pack.data[2] == parameter_id &&
                          std::equal(expected_payload.begin(), expected_payload.end(),
                                     pack.data + 3);
               },
               [](const MotorPackMsg&, bool& result) {
                   result = true;
               })
        .value_or(false);
}
void Motor::CancelWaitersWithoutDrain() noexcept {
    std::vector<std::shared_ptr<Impl::Waiter>> waiters;
    std::function<void()> on_cancelled;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->waiter_mutex);
        if (impl_->rejecting_waiters) {
            return;
        }
        impl_->rejecting_waiters = true;
        waiters.swap(impl_->waiters);
        on_cancelled = impl_->test_on_waiters_cancelled;
    }
    try {
        if (on_cancelled) {
            on_cancelled();
        }
    } catch (...) {}
    for (const auto& waiter : waiters) {
        platform::LockGuard<platform::Mutex> lock(waiter->mutex);
        waiter->cancelled.store(true, std::memory_order_release);
        waiter->cv.notify_all();
    }
}

bool Motor::WaitersDrained() const noexcept {
    return impl_->active_waiter_operations.load(std::memory_order_acquire) == 0;
}

void Motor::CancelWaiters() noexcept {
    CancelWaitersWithoutDrain();
    platform::UniqueLock<platform::Mutex> lock(impl_->waiter_mutex);
    platform::WaitForPredicate(impl_->waiter_operations_drained, lock, [this] {
        return WaitersDrained();
    });
}
void Motor::OnMessage(const MotorPackMsg& message) {
    MotorStatus decoded = AutoDecodeFeedback(message, impl_->current_range.load());
    if (decoded.error != MotorError::NoResponse) {
        {
            platform::LockGuard<platform::Mutex> lock(impl_->status_mutex);
            decoded.position =
                FilterStatusValue(decoded.position, impl_->last_accepted_status.position,
                                  impl_->status_filter_samples.position,
                                  impl_->status_limit_filter_max_deltas.position,
                                  impl_->status_median_filter_window_size);
            decoded.speed = FilterStatusValue(decoded.speed, impl_->last_accepted_status.speed,
                                              impl_->status_filter_samples.speed,
                                              impl_->status_limit_filter_max_deltas.speed,
                                              impl_->status_median_filter_window_size);
            decoded.current = FilterStatusValue(
                decoded.current, impl_->last_accepted_status.current,
                impl_->status_filter_samples.current, impl_->status_limit_filter_max_deltas.current,
                impl_->status_median_filter_window_size);
            decoded.motor_temperature = FilterStatusValue(
                decoded.motor_temperature, impl_->last_accepted_status.motor_temperature,
                impl_->status_filter_samples.motor_temperature,
                impl_->status_limit_filter_max_deltas.motor_temperature,
                impl_->status_median_filter_window_size);
            decoded.mos_temperature = FilterStatusValue(
                decoded.mos_temperature, impl_->last_accepted_status.mos_temperature,
                impl_->status_filter_samples.mos_temperature,
                impl_->status_limit_filter_max_deltas.mos_temperature,
                impl_->status_median_filter_window_size);
            impl_->status = decoded;
            impl_->status_life_cycle = impl_->max_status_life_cycle;
        }
    }

    std::vector<std::shared_ptr<Impl::Waiter>> waiters;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->waiter_mutex);
        waiters = impl_->waiters;
    }
    for (const auto& waiter : waiters) {
        platform::LockGuard<platform::Mutex> lock(waiter->mutex);
        if (waiter->cancelled.load(std::memory_order_acquire) ||
            waiter->completed.load(std::memory_order_acquire)) {
            continue;
        }
        try {
            std::function<void()> on_checker;
            std::function<void()> on_writer;
            {
                platform::LockGuard<platform::Mutex> hooks_lock(impl_->waiter_mutex);
                on_checker = impl_->test_on_waiter_checker;
                on_writer = impl_->test_on_waiter_writer;
            }
            if (on_checker) {
                on_checker();
            }
            if (!waiter->checker(message)) {
                continue;
            }
            if (on_writer) {
                on_writer();
            }
            waiter->writer(message, waiter->content.get());
            waiter->completed.store(true, std::memory_order_release);
            waiter->cv.notify_all();
        } catch (...) {
            if (impl_->logger_) {
                impl_->logger_->error("Motor response waiter threw an exception");
            }
            waiter->cancelled.store(true, std::memory_order_release);
            waiter->cv.notify_all();
        }
    }

    if (decoded.error != MotorError::NoResponse) {
        try {
            HandleStatus(decoded);
        } catch (...) {
            if (impl_->logger_) {
                impl_->logger_->error("Motor status callback threw an exception");
            }
        }
    }
}
void Motor::SetMaxStatusLifeCycle(int max_life_cycle) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    SetStatusLifeCycle(max_life_cycle);
}

void Motor::SetStatusLifeCycle(int max_life_cycle) {
    platform::LockGuard<platform::Mutex> lock(impl_->status_mutex);
    impl_->max_status_life_cycle = max_life_cycle;
    if (impl_->status && max_life_cycle != std::numeric_limits<int>::max()) {
        impl_->status_life_cycle = std::min(impl_->status_life_cycle, max_life_cycle);
    }
}

void Motor::SetStatusMedianFilterWindowSize(std::size_t median_window_size) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    SetStatusMedianFilterWindowSizeImpl(median_window_size);
}

void Motor::SetStatusMedianFilterWindowSizeImpl(std::size_t median_window_size) {
    platform::LockGuard<platform::Mutex> lock(impl_->status_mutex);
    impl_->status_median_filter_window_size = median_window_size;
    impl_->status_filter_samples = {};
}

void Motor::SetStatusLimitFilterMaxDeltas(float position, float speed, float current,
                                          float motor_temperature, float mos_temperature) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    MotorStatus max_deltas{};
    max_deltas.position = position;
    max_deltas.speed = speed;
    max_deltas.current = current;
    max_deltas.motor_temperature = motor_temperature;
    max_deltas.mos_temperature = mos_temperature;
    SetStatusLimitFilterMaxDeltasImpl(max_deltas);
}

void Motor::SetStatusLimitFilterMaxDeltas(const MotorStatus& limit_max_deltas) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    SetStatusLimitFilterMaxDeltasImpl(limit_max_deltas);
}

void Motor::SetStatusLimitFilterMaxDeltasImpl(const MotorStatus& limit_max_deltas) {
    platform::LockGuard<platform::Mutex> lock(impl_->status_mutex);
    impl_->status_limit_filter_max_deltas = limit_max_deltas;
}
Motor::~Motor() {
    CancelWaiters();
    platform::LockGuard<platform::RecursiveMutex> operation_lock(impl_->motor_mutex);
    try {
        DisableLogImpl(false);
    } catch (const std::exception& error) {
        if (impl_->logger_) {
            impl_->logger_->error("Failed to flush motor logs during destruction: {}",
                                  error.what());
        }
    } catch (...) {
        if (impl_->logger_) {
            impl_->logger_->error("Failed to flush motor logs during destruction");
        }
    }
}
MotorPVTRanges Motor::GetPVTRanges() const {
    auto operation =
        EncosDriverManager::Instance().AcquireDeviceOperation(const_cast<Motor*>(this));
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    return impl_->ranges;
}

void Motor::SetDriverPVTRanges(const MotorPVTRanges& ranges) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    impl_->ranges.kp = ranges.kp;
    impl_->ranges.kd = ranges.kd;
    impl_->ranges.position = ranges.position;
    impl_->ranges.speed = ranges.speed;
    impl_->ranges.torque = ranges.torque;
    impl_->ranges.current = ranges.current;
}

void Motor::SetCurrentRange(float range) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    impl_->current_range = range;
}

void Motor::EnableCanFd() {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    impl_->frame_flags.fetch_or(kCanFrameFlagFdMask, std::memory_order_relaxed);
}

void Motor::DisableCanFd() {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    impl_->frame_flags.fetch_and(static_cast<uint8_t>(~kCanFrameFlagFdMask),
                                 std::memory_order_relaxed);
}

bool Motor::IsCanFdEnabled() const {
    auto operation =
        EncosDriverManager::Instance().AcquireDeviceOperation(const_cast<Motor*>(this));
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    return CanFrameFlagsUseCanFd(impl_->frame_flags.load(std::memory_order_relaxed));
}

void Motor::EnableCanEff() {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    impl_->frame_flags.fetch_or(kCanFrameFlagEff, std::memory_order_relaxed);
}

void Motor::DisableCanEff() {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    impl_->frame_flags.fetch_and(static_cast<uint8_t>(~kCanFrameFlagEff),
                                 std::memory_order_relaxed);
}

bool Motor::IsCanEffEnabled() const {
    auto operation =
        EncosDriverManager::Instance().AcquireDeviceOperation(const_cast<Motor*>(this));
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    return CanFrameFlagsUseExtendedId(impl_->frame_flags.load(std::memory_order_relaxed));
}
void Motor::SendMessageLocked(const MotorPackMsg& msg) {
    MotorPackMsg packet = msg;
    packet.frame_flags = SanitizeCanFrameFlags(impl_->frame_flags.load(std::memory_order_relaxed));
    impl_->writer(packet);
}
std::optional<MotorStatus> Motor::GetStatus(int life_cycle_deduction) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    return GetStatusImpl(life_cycle_deduction);
}

std::optional<MotorStatus> Motor::GetStatusImpl(int life_cycle_deduction) {
    platform::LockGuard<platform::Mutex> lock(impl_->status_mutex);
    if (!impl_->status) {
        return std::nullopt;
    }
    const auto status = impl_->status;
    if (impl_->status_life_cycle != std::numeric_limits<int>::max() && life_cycle_deduction > 0) {
        impl_->status_life_cycle = life_cycle_deduction >= impl_->status_life_cycle
                                       ? 0
                                       : impl_->status_life_cycle - life_cycle_deduction;
        if (impl_->status_life_cycle <= 0) {
            impl_->status.reset();
            impl_->status_filter_samples.position.clear();
            impl_->status_filter_samples.current.clear();
            impl_->status_filter_samples.motor_temperature.clear();
            impl_->status_filter_samples.mos_temperature.clear();
            impl_->last_accepted_status.position = std::numeric_limits<float>::quiet_NaN();
            impl_->last_accepted_status.current = std::numeric_limits<float>::quiet_NaN();
            impl_->last_accepted_status.motor_temperature = std::numeric_limits<float>::quiet_NaN();
            impl_->last_accepted_status.mos_temperature = std::numeric_limits<float>::quiet_NaN();
        }
    }
    return status;
}
void Motor::SetOnStatus(std::function<void(const MotorStatus&)> callback) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    SetOnStatusImpl(std::move(callback));
}

void Motor::SetOnStatusImpl(std::function<void(const MotorStatus&)> callback) {
    {
        platform::LockGuard<platform::Mutex> lock(impl_->callback_mutex);
        impl_->user_on_status = std::move(callback);
    }
    UpdateStatusDispatcher();
}

void Motor::UpdateStatusDispatcher() {
    // 状态在电机接收路由中统一分发；此入口保留以兼容日志状态切换。
}

void Motor::HandleStatus(const MotorStatus& status) {
    const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    std::function<void(const MotorStatus&)> user_callback;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->callback_mutex);
        user_callback = impl_->user_on_status;
    }
    std::shared_ptr<detail::MotorLogSession> log_session;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->log_mutex);
        log_session = impl_->log_session;
    }
    if (log_session) {
        try {
            detail::RethrowMotorLogSessionError(log_session);
            detail::MotorStatusLogRecord record;
            record.timestamp_ns = timestamp;
            record.error = static_cast<int>(status.error);
            record.position = status.position;
            record.speed = status.speed;
            record.current = status.current;
            record.motor_temperature = status.motor_temperature;
            record.mos_temperature = status.mos_temperature;
            detail::PublishMotorStatusLogRecord(log_session, record);
        } catch (...) {
            (void) RecoverLogSession("status");
        }
    }
    if (user_callback) {
        user_callback(status);
    }
}

}  // namespace encos
