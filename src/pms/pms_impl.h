#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include "platform/sync.h"
#include "pms/pms.h"
#include "protocol/route_ids.h"

namespace encos {

struct Pms::Impl {
    static constexpr std::chrono::seconds state_timeout{2};
    static constexpr uint8_t kBaseStateUpdated = 1u << 0u;
    static constexpr uint8_t kV48Current1To4Updated = 1u << 1u;
    static constexpr uint8_t kV48AndV19CurrentUpdated = 1u << 2u;
    static constexpr uint8_t kAllStateFramesUpdated =
        kBaseStateUpdated | kV48Current1To4Updated | kV48AndV19CurrentUpdated;

    bool OnMessage(const MotorPackMsg& message);
    bool IsStatusValid(std::chrono::steady_clock::time_point now) const;
    std::optional<PmsStatus> GetStatusSnapshot();
    void SendCommand(PmsCommand command);

    Bus* bus = nullptr;
    std::function<void(const MotorPackMsg&)> writer;
    LoggerPtr logger_;
    std::chrono::steady_clock::time_point last_base_state_update{};
    std::chrono::steady_clock::time_point last_v48_current_1_to_4_update{};
    std::chrono::steady_clock::time_point last_v48_and_v19_currents_update{};
    uint8_t updated_since_callback = 0;
    PmsStatus current_status{};
    std::function<void(const PmsStatus&)> on_status;
    platform::Mutex status_mutex;
    platform::Mutex command_mutex;
};

}  // namespace encos
