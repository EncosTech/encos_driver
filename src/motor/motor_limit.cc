#include <chrono>
#include <cmath>
#include <limits>

#include "encos/driver_manager.h"
#include "motor/motor.h"
#include "motor/motor_impl.h"
#include "platform/delay.h"
#include "platform/wait.h"
#include "utils/constants.h"

namespace encos {

float Motor::GotoLimit(Range<float> limit, int dir, float spd, float cur,
                       std::chrono::milliseconds timeout) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        return std::numeric_limits<float>::quiet_NaN();
    if (limit.min >= limit.max)
        return std::numeric_limits<float>::quiet_NaN();
    if (dir == 0)
        return std::numeric_limits<float>::quiet_NaN();

    spd = std::abs(spd);
    cur = std::abs(cur);

    int fail_count = 0;
    int success_count = 0;
    float spd_cmd = (dir > 0 ? 1 : -1) * spd;
    float limit_pos = (dir > 0 ? limit.max : limit.min);

    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        auto feedback = SpdControl<1>(spd_cmd, cur);
        if (feedback.error != MotorError::NoError) {
            fail_count++;
            if (fail_count >= 5) {
                impl_->logger_->error("Motor {} GotoLimit failed due to communication error",
                                      impl_->idx.load(std::memory_order_relaxed));
                return std::numeric_limits<float>::quiet_NaN();
            }
        } else {
            fail_count = 0;
            if (std::abs(feedback.speed) < 1.f / 180 * M_PIf &&
                std::abs(feedback.current) > cur / 2.f) {
                success_count++;
                if (success_count >= 5) {
                    float limit_pos_actual = feedback.position;
                    float offset = limit_pos_actual - limit_pos;
                    impl_->logger_->info(
                        "Motor {} reached limit position: {:.3f}, calculated zero position: {:.3f}",
                        impl_->idx.load(std::memory_order_relaxed), limit_pos_actual, offset);
                    return offset;
                }
            } else {
                success_count = 0;
            }
        }
        if (std::chrono::steady_clock::now() - start_time > timeout) {
            impl_->logger_->error("Motor {} GotoLimit timed out",
                                  impl_->idx.load(std::memory_order_relaxed));
            return std::numeric_limits<float>::quiet_NaN();
        }
        platform::SleepFor(std::chrono::milliseconds(50));
    }
}
bool Motor::GotoZero(float offset, float spd, float cur, std::chrono::milliseconds timeout) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        return false;

    try {
        const float current_pos = GetParameter<MotorParameter::Position>();
        return SetPos(static_cast<double>(current_pos - offset));
    } catch (const std::exception&) {
        // Fall back to the legacy move-and-reset flow below.
    }

    spd = std::abs(spd);
    cur = std::abs(cur);

    int fail_count = 0;
    int success_count = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        auto feedback = PosControl<1>(offset, spd, cur);
        if (feedback.error != MotorError::NoError) {
            fail_count++;
            if (fail_count >= 5) {
                impl_->logger_->error("Motor {} GotoZero failed due to communication error",
                                      impl_->idx.load(std::memory_order_relaxed));
                return false;
            }
        } else {
            fail_count = 0;
            if (std::abs(feedback.speed) < 1.f / 180 * M_PIf &&
                std::abs(feedback.position - offset) < 1.f / 180 * M_PIf) {
                success_count++;
                if (success_count >= 5) {
                    break;
                }
            } else {
                success_count = 0;
            }
        }
        if (std::chrono::steady_clock::now() - start_time > timeout) {
            impl_->logger_->error("Motor {} GotoZero timed out",
                                  impl_->idx.load(std::memory_order_relaxed));
            return false;
        }
        platform::SleepFor(std::chrono::milliseconds(50));
    }

    bool res = ResetZeroPos(true);
    if (res) {
        impl_->logger_->info("Motor {} GotoZero successful",
                             impl_->idx.load(std::memory_order_relaxed));
    } else {
        impl_->logger_->error("Motor {} GotoZero failed during zero position reset",
                              impl_->idx.load(std::memory_order_relaxed));
    }
    return res;
}
bool Motor::Calibrate(Range<float> limit, int dir, float spd, float cur,
                      std::chrono::milliseconds timeout) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    float offset = GotoLimit(limit, dir, spd, cur, timeout);
    if (std::isnan(offset))
        return false;
    return GotoZero(offset, spd, cur, timeout);
}

}  // namespace encos
