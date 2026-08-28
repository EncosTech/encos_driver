#include "motor/waiter_test_access.h"

#include <mutex>
#include <utility>
#include <vector>

#include "motor/motor_impl.h"

namespace encos {

std::unique_ptr<Motor> MotorWaiterTestAccess::CreateWithModel(Bus* bus, MotorModel model,
                                                              uint8_t frame_flags, bool canfd) {
    return std::unique_ptr<Motor>(new Motor(
        bus, 100, model, CreateLogger("motor_constructor_model_test"), [](const MotorPackMsg&) {},
        frame_flags, canfd));
}

std::unique_ptr<Motor> MotorWaiterTestAccess::CreateWithRanges(Bus* bus, MotorPVTRanges ranges,
                                                               uint8_t frame_flags, bool canfd) {
    return std::unique_ptr<Motor>(new Motor(
        bus, 101, ranges, CreateLogger("motor_constructor_ranges_test"), [](const MotorPackMsg&) {},
        frame_flags, canfd));
}

std::unique_ptr<Motor> MotorWaiterTestAccess::CreateForFirmwareInitialization(Bus* bus,
                                                                              uint8_t frame_flags,
                                                                              bool canfd) {
    return std::unique_ptr<Motor>(new Motor(
        bus, 102, CreateLogger("motor_constructor_firmware_test"), [](const MotorPackMsg&) {},
        frame_flags, canfd));
}

uint8_t MotorWaiterTestAccess::FrameFlags(const Motor* motor) {
    return motor->impl_->frame_flags.load(std::memory_order_relaxed);
}

bool MotorWaiterTestAccess::TransactionMutexIsLocked(Motor* motor) {
    if (motor->impl_->motor_mutex.try_lock()) {
        motor->impl_->motor_mutex.unlock();
        return false;
    }
    return true;
}

bool MotorWaiterTestAccess::LogSessionHasError(Motor* motor) {
    platform::LockGuard<platform::Mutex> lock(motor->impl_->log_mutex);
    return motor->impl_->log_session && detail::MotorLogSessionHasError(motor->impl_->log_session);
}

void MotorWaiterTestAccess::SetHooks(Motor* motor, std::function<void()> on_registered,
                                     std::function<void()> on_checker,
                                     std::function<void()> on_writer) {
    platform::LockGuard<platform::Mutex> lock(motor->impl_->waiter_mutex);
    motor->impl_->test_on_waiter_registered = std::move(on_registered);
    motor->impl_->test_on_waiter_checker = std::move(on_checker);
    motor->impl_->test_on_waiter_writer = std::move(on_writer);
}

void MotorWaiterTestAccess::SetCancellationHook(Motor* motor, std::function<void()> on_cancelled) {
    platform::LockGuard<platform::Mutex> lock(motor->impl_->waiter_mutex);
    motor->impl_->test_on_waiters_cancelled = std::move(on_cancelled);
}

void MotorWaiterTestAccess::NotifyAll(Motor* motor) {
    std::vector<std::shared_ptr<Motor::Impl::Waiter>> waiters;
    {
        platform::LockGuard<platform::Mutex> lock(motor->impl_->waiter_mutex);
        waiters = motor->impl_->waiters;
    }
    for (const auto& waiter : waiters) {
        waiter->cv.notify_all();
    }
}

std::optional<MotorPackMsg> MotorWaiterTestAccess::WaitForPacket(
    Motor* motor, const MotorPackMsg& request, std::function<bool(const MotorPackMsg&)> checker,
    std::chrono::milliseconds timeout) {
    return motor->SendAndWaitTyped<MotorPackMsg>(
        request, std::move(checker),
        [](const MotorPackMsg& message, MotorPackMsg& result) {
            result = message;
        },
        timeout);
}

std::optional<bool> MotorWaiterTestAccess::WaitForBoolean(
    Motor* motor, const MotorPackMsg& request, std::function<bool(const MotorPackMsg&)> checker,
    std::chrono::milliseconds timeout) {
    return motor->SendAndWaitTyped<bool>(
        request, std::move(checker),
        [](const MotorPackMsg&, bool& result) {
            result = true;
        },
        timeout);
}

}  // namespace encos
