#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include "battery/battery.h"
#include "platform/sync.h"

namespace encos {

struct Battery::Impl {
    static constexpr std::chrono::seconds state_timeout{1};
    bool OnMessage(const MotorPackMsg& message);
    BatteryStatus GetStatusSnapshot();
    void SendPassiveCommands(const BatteryPassiveCommands& commands);

    uint16_t idx = 0;
    Bus* bus = nullptr;
    std::function<void(const MotorPackMsg&)> writer;
    LoggerPtr logger_;
    std::chrono::steady_clock::time_point last_state_update{};
    std::chrono::steady_clock::time_point last_temp_update{};
    std::chrono::steady_clock::time_point last_error_update{};
    std::chrono::steady_clock::time_point last_active_commands_update{};
    BatteryStatus current_status{};
    std::function<void(const BatteryStatus&)> on_status;
    platform::Mutex status_mutex;
    platform::Mutex command_mutex;
};

}  // namespace encos
