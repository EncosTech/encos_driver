#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

#include "utils/port.h"

namespace encos::detail {

enum class MotorCommandLogType : std::uint8_t {
    PVTControl,
    PosControl,
    SpdControl,
    CurControl,
    TorControl,
    Stop,
    Brake,
};

enum MotorCommandLogField : std::uint16_t {
    kMotorCommandLogKp = 1U << 0U,
    kMotorCommandLogKd = 1U << 1U,
    kMotorCommandLogPosition = 1U << 2U,
    kMotorCommandLogSpeed = 1U << 3U,
    kMotorCommandLogCurrent = 1U << 4U,
    kMotorCommandLogTorque = 1U << 5U,
    kMotorCommandLogStopMode = 1U << 6U,
    kMotorCommandLogBrakeEnabled = 1U << 7U,
    kMotorCommandLogFeedback = 1U << 8U,
};

struct MotorCommandLogRecord {
    std::int64_t timestamp_ns{0};
    float kp{0.0F};
    float kd{0.0F};
    float position{0.0F};
    float speed{0.0F};
    float current{0.0F};
    float torque{0.0F};
    std::int32_t stop_mode{0};
    std::int32_t feedback{0};
    std::uint16_t fields{0};
    MotorCommandLogType type{MotorCommandLogType::PVTControl};
    bool brake_enabled{false};
};

struct MotorStatusLogRecord {
    std::int64_t timestamp_ns{0};
    std::int32_t error{0};
    float position{0.0F};
    float speed{0.0F};
    float current{0.0F};
    float motor_temperature{0.0F};
    float mos_temperature{0.0F};
};

static_assert(std::is_trivially_copyable_v<MotorCommandLogRecord>);
static_assert(std::is_trivially_copyable_v<MotorStatusLogRecord>);

inline constexpr std::size_t kMotorCommandLogPortCapacity = 1024U;
inline constexpr std::size_t kMotorStatusLogPortCapacity = 1024U;

struct MotorLogPortSession {
    Port<kMotorCommandLogPortCapacity, MotorCommandLogRecord> command_port;
    Port<kMotorStatusLogPortCapacity, MotorStatusLogRecord> status_port;
};

class MotorLogSession {
public:
    virtual ~MotorLogSession() = default;
};

std::shared_ptr<MotorLogSession> CreateMotorLogSession(const std::string& base_name);
void PublishMotorCommandLogRecord(const std::shared_ptr<MotorLogSession>& session,
                                  MotorCommandLogRecord record) noexcept;
void PublishMotorStatusLogRecord(const std::shared_ptr<MotorLogSession>& session,
                                 MotorStatusLogRecord record) noexcept;
void RethrowMotorLogSessionError(const std::shared_ptr<MotorLogSession>& session);
bool MotorLogSessionHasError(const std::shared_ptr<MotorLogSession>& session) noexcept;
void CloseMotorLogSession(const std::shared_ptr<MotorLogSession>& session);

}  // namespace encos::detail
