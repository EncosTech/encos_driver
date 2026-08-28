#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include "encos/driver_manager.h"
#include "motor/motor.h"
#include "motor/motor_impl.h"

namespace encos {
namespace {

std::uint16_t CommandFields(std::optional<float> kp, std::optional<float> kd,
                            std::optional<float> position, std::optional<float> speed,
                            std::optional<float> current, std::optional<float> torque,
                            std::optional<MotorStopMode> stop_mode,
                            std::optional<bool> brake_enabled, std::optional<int> feedback) {
    std::uint16_t fields = 0;
    fields |= kp ? detail::kMotorCommandLogKp : 0U;
    fields |= kd ? detail::kMotorCommandLogKd : 0U;
    fields |= position ? detail::kMotorCommandLogPosition : 0U;
    fields |= speed ? detail::kMotorCommandLogSpeed : 0U;
    fields |= current ? detail::kMotorCommandLogCurrent : 0U;
    fields |= torque ? detail::kMotorCommandLogTorque : 0U;
    fields |= stop_mode ? detail::kMotorCommandLogStopMode : 0U;
    fields |= brake_enabled ? detail::kMotorCommandLogBrakeEnabled : 0U;
    fields |= feedback ? detail::kMotorCommandLogFeedback : 0U;
    return fields;
}

detail::MotorCommandLogType CommandType(const char* type) {
    if (std::string_view(type) == "PVTControl") {
        return detail::MotorCommandLogType::PVTControl;
    }
    if (std::string_view(type) == "PosControl") {
        return detail::MotorCommandLogType::PosControl;
    }
    if (std::string_view(type) == "SpdControl") {
        return detail::MotorCommandLogType::SpdControl;
    }
    if (std::string_view(type) == "CurControl") {
        return detail::MotorCommandLogType::CurControl;
    }
    if (std::string_view(type) == "TorControl") {
        return detail::MotorCommandLogType::TorControl;
    }
    if (std::string_view(type) == "Stop") {
        return detail::MotorCommandLogType::Stop;
    }
    return detail::MotorCommandLogType::Brake;
}

}  // namespace

void Motor::EnableLog(const std::string& base_name) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    {
        platform::LockGuard<platform::Mutex> lock(impl_->log_mutex);
        if (impl_->log_session && impl_->log_base_name == base_name) {
            return;
        }
    }

    if (IsLogged()) {
        DisableLog();
    }

    auto session = detail::CreateMotorLogSession(base_name);
    {
        platform::LockGuard<platform::Mutex> lock(impl_->log_mutex);
        impl_->log_session = std::move(session);
        impl_->log_base_name = base_name;
    }
    UpdateStatusDispatcher();
}

void Motor::DisableLog() {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    DisableLogImpl(true);
}

void Motor::DisableLogImpl(bool update_dispatcher) {
    std::shared_ptr<detail::MotorLogSession> session;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->log_mutex);
        session = std::move(impl_->log_session);
        impl_->log_base_name.clear();
    }
    if (update_dispatcher) {
        UpdateStatusDispatcher();
    }
    if (session) {
        detail::CloseMotorLogSession(session);
    }
}

bool Motor::IsLogged() const {
    auto operation =
        EncosDriverManager::Instance().AcquireDeviceOperation(const_cast<Motor*>(this));
    platform::LockGuard<platform::Mutex> lock(impl_->log_mutex);
    return static_cast<bool>(impl_->log_session);
}

void Motor::RecordCommand(const char* type, std::optional<float> kp, std::optional<float> kd,
                          std::optional<float> position, std::optional<float> speed,
                          std::optional<float> current, std::optional<float> torque,
                          std::optional<MotorStopMode> stop_mode, std::optional<bool> brake_enabled,
                          std::optional<int> feedback) noexcept {
    for (;;) {
        std::shared_ptr<detail::MotorLogSession> session;
        {
            platform::LockGuard<platform::Mutex> lock(impl_->log_mutex);
            session = impl_->log_session;
        }
        if (!session) {
            return;
        }
        try {
            detail::RethrowMotorLogSessionError(session);
        } catch (...) {
            if (!RecoverLogSession("command")) {
                return;
            }
            continue;
        }

        detail::MotorCommandLogRecord record;
        record.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
        record.type = CommandType(type);
        record.fields = CommandFields(kp, kd, position, speed, current, torque, stop_mode,
                                      brake_enabled, feedback);
        record.kp = kp.value_or(0.0F);
        record.kd = kd.value_or(0.0F);
        record.position = position.value_or(0.0F);
        record.speed = speed.value_or(0.0F);
        record.current = current.value_or(0.0F);
        record.torque = torque.value_or(0.0F);
        record.stop_mode = stop_mode ? static_cast<int>(*stop_mode) : 0;
        record.brake_enabled = brake_enabled.value_or(false);
        record.feedback = feedback.value_or(0);
        detail::PublishMotorCommandLogRecord(session, record);
        {
            platform::LockGuard<platform::Mutex> lock(impl_->log_mutex);
            if (++impl_->log_success_count >= 10) {
                impl_->log_retry_budget = 3;
                impl_->log_success_count = 0;
            }
        }
        return;
    }
}

bool Motor::RecoverLogSession(const char* context) noexcept {
    std::string base_name;
    std::shared_ptr<detail::MotorLogSession> old_session;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->log_mutex);
        if (impl_->log_recovering || impl_->log_base_name.empty()) {
            return false;
        }
        impl_->log_recovering = true;
        base_name = impl_->log_base_name;
        old_session = std::move(impl_->log_session);
    }
    if (impl_->logger_) {
        impl_->logger_->error("Motor {} {} logging failed; rebuilding log session",
                              impl_->idx.load(std::memory_order_relaxed), context);
    }
    try {
        detail::CloseMotorLogSession(old_session);
    } catch (...) {}
    for (;;) {
        {
            platform::LockGuard<platform::Mutex> lock(impl_->log_mutex);
            if (impl_->log_base_name != base_name || impl_->log_retry_budget <= 0) {
                impl_->log_base_name.clear();
                impl_->log_recovering = false;
                return false;
            }
            --impl_->log_retry_budget;
        }
        try {
            auto session = detail::CreateMotorLogSession(base_name);
            platform::LockGuard<platform::Mutex> lock(impl_->log_mutex);
            if (impl_->log_base_name != base_name) {
                impl_->log_recovering = false;
                return false;
            }
            impl_->log_session = std::move(session);
            impl_->log_recovering = false;
            return true;
        } catch (const std::exception& error) {
            if (impl_->logger_) {
                impl_->logger_->error("Failed to rebuild motor log session: {}", error.what());
            }
        } catch (...) {}
    }
}

}  // namespace encos
