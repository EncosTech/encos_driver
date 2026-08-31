#include "motor/motor_log_transport.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <optional>
#include <thread>
#include <vector>

#include "platform/sync.h"
#include "utils/log_writer.h"

namespace encos::detail {
class MotorLogSessionImpl;

namespace {

class MotorLogWorker {
public:
    MotorLogWorker()
        : thread_([this]() {
              Run();
          }) {}

    void Register(const std::shared_ptr<MotorLogSessionImpl>& session) {
        platform::LockGuard<platform::Mutex> lock(mutex_);
        sessions_.push_back(session);
        cv_.notify_one();
    }

    void Unregister(const std::shared_ptr<MotorLogSessionImpl>& session) {
        platform::LockGuard<platform::Mutex> lock(mutex_);
        sessions_.erase(std::remove(sessions_.begin(), sessions_.end(), session), sessions_.end());
    }

    void Notify() noexcept {
        cv_.notify_one();
    }

private:
    void Run() noexcept;

    platform::Mutex mutex_;
    std::condition_variable_any cv_;
    std::vector<std::shared_ptr<MotorLogSessionImpl>> sessions_;
    std::thread thread_;
};

MotorLogWorker& Worker() {
    // 管理器析构时仍需关闭电机日志，因此 worker 必须覆盖全部静态对象的析构阶段。
    static auto* const worker = new MotorLogWorker();
    return *worker;
}

}  // namespace

class MotorLogSessionImpl : public MotorLogSession {
public:
    explicit MotorLogSessionImpl(const std::string& base_name)
        : status_writer_(
              base_name + "_status",
              std::array<std::string, 7>{"timestamp_ns", "error", "position", "speed", "current",
                                         "motor_temperature", "mos_temperature"}),
          command_writer_(base_name + "_command",
                          std::array<std::string, 11>{"timestamp_ns", "type", "kp", "kd",
                                                      "position", "speed", "current", "torque",
                                                      "stop_mode", "brake_enabled", "feedback"}) {}

    void PublishCommand(MotorCommandLogRecord record) noexcept {
        if (closing_.load(std::memory_order_acquire)) {
            return;
        }
        ports_.command_port.Push(record);
    }

    void PublishStatus(MotorStatusLogRecord record) noexcept {
        if (closing_.load(std::memory_order_acquire)) {
            return;
        }
        ports_.status_port.Push(record);
    }

    void Close() {
        closing_.store(true, std::memory_order_release);
        {
            platform::UniqueLock<platform::Mutex> lock(completion_mutex_);
            close_requested_ = true;
        }
        Worker().Notify();
        platform::UniqueLock<platform::Mutex> lock(completion_mutex_);
        completion_cv_.wait(lock, [this]() {
            return closed_;
        });
        if (error_) {
            std::rethrow_exception(error_);
        }
    }

private:
    friend class MotorLogWorker;

    static const char* CommandType(MotorCommandLogType type) {
        switch (type) {
            case MotorCommandLogType::PVTControl:
                return "PVTControl";
            case MotorCommandLogType::PosControl:
                return "PosControl";
            case MotorCommandLogType::SpdControl:
                return "SpdControl";
            case MotorCommandLogType::CurControl:
                return "CurControl";
            case MotorCommandLogType::TorControl:
                return "TorControl";
            case MotorCommandLogType::Stop:
                return "Stop";
            case MotorCommandLogType::Brake:
                return "Brake";
        }
        return "Unknown";
    }

public:
    void Drain() noexcept {
        if (failed_.load(std::memory_order_acquire)) {
            FinalizeIfReady();
            return;
        }
        try {
            while (const auto record = ports_.command_port.Pop()) {
                command_writer_.write(
                    record->timestamp_ns, CommandType(record->type),
                    (record->fields & kMotorCommandLogKp) ? std::optional<float>(record->kp)
                                                          : std::nullopt,
                    (record->fields & kMotorCommandLogKd) ? std::optional<float>(record->kd)
                                                          : std::nullopt,
                    (record->fields & kMotorCommandLogPosition)
                        ? std::optional<float>(record->position)
                        : std::nullopt,
                    (record->fields & kMotorCommandLogSpeed) ? std::optional<float>(record->speed)
                                                             : std::nullopt,
                    (record->fields & kMotorCommandLogCurrent)
                        ? std::optional<float>(record->current)
                        : std::nullopt,
                    (record->fields & kMotorCommandLogTorque) ? std::optional<float>(record->torque)
                                                              : std::nullopt,
                    (record->fields & kMotorCommandLogStopMode)
                        ? std::optional<int>(record->stop_mode)
                        : std::nullopt,
                    (record->fields & kMotorCommandLogBrakeEnabled)
                        ? std::optional<bool>(record->brake_enabled)
                        : std::nullopt,
                    (record->fields & kMotorCommandLogFeedback)
                        ? std::optional<int>(record->feedback)
                        : std::nullopt);
            }
            while (const auto record = ports_.status_port.Pop()) {
                status_writer_.write(record->timestamp_ns, record->error, record->position,
                                     record->speed, record->current, record->motor_temperature,
                                     record->mos_temperature);
            }
            FinalizeIfReady();
        } catch (...) {
            StoreError(std::current_exception());
            FinalizeIfReady();
        }
    }

    void RethrowError() {
        if (!failed_.load(std::memory_order_acquire)) {
            return;
        }
        platform::LockGuard<platform::Mutex> lock(completion_mutex_);
        std::rethrow_exception(error_);
    }

    bool HasError() const noexcept {
        return failed_.load(std::memory_order_acquire);
    }

private:
    void FinalizeIfReady() noexcept {
        bool finalize = false;
        {
            platform::LockGuard<platform::Mutex> lock(completion_mutex_);
            finalize = close_requested_ && !closed_ &&
                       (failed_.load(std::memory_order_acquire) ||
                        (ports_.command_port.Empty() && ports_.status_port.Empty()));
        }
        if (!finalize) {
            return;
        }
        try {
            command_writer_.flush();
            status_writer_.flush();
        } catch (...) {
            StoreError(std::current_exception());
        }
        {
            platform::LockGuard<platform::Mutex> lock(completion_mutex_);
            closed_ = true;
        }
        completion_cv_.notify_all();
    }

    void StoreError(std::exception_ptr error) noexcept {
        platform::LockGuard<platform::Mutex> lock(completion_mutex_);
        if (!error_) {
            error_ = error;
            failed_.store(true, std::memory_order_release);
        }
    }

    MotorLogPortSession ports_;
    LogWriter<7> status_writer_;
    LogWriter<11> command_writer_;
    std::atomic<bool> closing_{false};
    platform::Mutex completion_mutex_;
    std::condition_variable_any completion_cv_;
    bool close_requested_ = false;
    bool closed_ = false;
    std::exception_ptr error_;
    std::atomic<bool> failed_{false};
};

namespace {

void MotorLogWorker::Run() noexcept {
    for (;;) {
        std::vector<std::shared_ptr<MotorLogSessionImpl>> sessions;
        {
            platform::UniqueLock<platform::Mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(1), [this]() {
                return !sessions_.empty();
            });
            sessions = sessions_;
        }
        for (const auto& session : sessions) {
            session->Drain();
        }
    }
}

}  // namespace

std::shared_ptr<MotorLogSession> CreateMotorLogSession(const std::string& base_name) {
    auto session = std::make_shared<MotorLogSessionImpl>(base_name);
    Worker().Register(session);
    return session;
}

void PublishMotorCommandLogRecord(const std::shared_ptr<MotorLogSession>& session,
                                  MotorCommandLogRecord record) noexcept {
    std::static_pointer_cast<MotorLogSessionImpl>(session)->PublishCommand(record);
    Worker().Notify();
}

void PublishMotorStatusLogRecord(const std::shared_ptr<MotorLogSession>& session,
                                 MotorStatusLogRecord record) noexcept {
    std::static_pointer_cast<MotorLogSessionImpl>(session)->PublishStatus(record);
    Worker().Notify();
}

void RethrowMotorLogSessionError(const std::shared_ptr<MotorLogSession>& session) {
    std::static_pointer_cast<MotorLogSessionImpl>(session)->RethrowError();
}

bool MotorLogSessionHasError(const std::shared_ptr<MotorLogSession>& session) noexcept {
    return std::static_pointer_cast<MotorLogSessionImpl>(session)->HasError();
}

void CloseMotorLogSession(const std::shared_ptr<MotorLogSession>& session) {
    const auto implementation = std::static_pointer_cast<MotorLogSessionImpl>(session);
    try {
        implementation->Close();
    } catch (...) {
        Worker().Unregister(implementation);
        throw;
    }
    Worker().Unregister(implementation);
}

}  // namespace encos::detail
