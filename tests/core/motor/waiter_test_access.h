#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "motor/motor.h"
#include "motor/types.h"
#include "platform/log.h"

namespace encos {

class MotorWaiterTestAccess {
public:
    static std::unique_ptr<Motor> CreateWithModel(Bus* bus, MotorModel model, uint8_t frame_flags,
                                                  bool canfd);
    static std::unique_ptr<Motor> CreateWithRanges(Bus* bus, MotorPVTRanges ranges,
                                                   uint8_t frame_flags, bool canfd);
    static std::unique_ptr<Motor> CreateForFirmwareInitialization(Bus* bus, uint8_t frame_flags,
                                                                  bool canfd);
    static uint8_t FrameFlags(const Motor* motor);
    static bool TransactionMutexIsLocked(Motor* motor);
    static bool LogSessionHasError(Motor* motor);
    static void SetHooks(Motor* motor, std::function<void()> on_registered,
                         std::function<void()> on_checker, std::function<void()> on_writer);
    static void SetCancellationHook(Motor* motor, std::function<void()> on_cancelled);
    static void NotifyAll(Motor* motor);
    static std::optional<MotorPackMsg> WaitForPacket(
        Motor* motor, const MotorPackMsg& request, std::function<bool(const MotorPackMsg&)> checker,
        std::chrono::milliseconds timeout);
    static std::optional<bool> WaitForBoolean(Motor* motor, const MotorPackMsg& request,
                                              std::function<bool(const MotorPackMsg&)> checker,
                                              std::chrono::milliseconds timeout);
};

}  // namespace encos
