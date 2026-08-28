#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include "imu/imu.h"
#include "platform/sync.h"
#include "protocol/route_ids.h"

namespace encos {

struct Imu::Impl {
    static constexpr std::chrono::seconds state_timeout{1};
    bool OnMessage(const MotorPackMsg& message);
    ImuStatus GetStatusSnapshot();

    uint16_t idx = 0;
    Bus* bus = nullptr;
    std::function<void(const MotorPackMsg&)> writer;
    LoggerPtr logger_;
    std::chrono::steady_clock::time_point last_acceleration_update{};
    std::chrono::steady_clock::time_point last_angular_velocity_update{};
    std::chrono::steady_clock::time_point last_euler_angle_update{};
    std::chrono::steady_clock::time_point last_quaternion_update{};
    ImuStatus current_status{};
    std::function<void(const ImuStatus&)> on_status;
    platform::Mutex status_mutex;
};

}  // namespace encos
