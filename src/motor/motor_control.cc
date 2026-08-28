#include <cstdint>
#include <limits>
#include <optional>

#include "encos/driver_manager.h"
#include "motor/math_utils.h"
#include "motor/motor.h"
#include "motor/motor_impl.h"
#include "motor/pack_helper.h"
#include "platform/wait.h"

namespace encos {

template <int FeedbackType>
auto Motor::PVTControl(float kp, float kd, float pos, float spd, float torque)
    -> std::conditional_t<FeedbackType == 0, void, MotorFeedbackMsg1> {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    static_assert(FeedbackType == 0 || FeedbackType == 1, "Invalid FeedbackType: must be 0 or 1");
    if (!impl_->bus) {
        if constexpr (FeedbackType == 0)
            return;
        else
            return {};
    }

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 8;

    kp = std::clamp(kp, impl_->ranges.kp.min, impl_->ranges.kp.max);
    kd = std::clamp(kd, impl_->ranges.kd.min, impl_->ranges.kd.max);
    pos = std::clamp(pos, impl_->ranges.position.min, impl_->ranges.position.max);
    spd = std::clamp(spd, impl_->ranges.speed.min, impl_->ranges.speed.max);
    torque = std::clamp(torque, impl_->ranges.torque.min, impl_->ranges.torque.max);

    uint16_t kp_int = FloatToUint(kp, impl_->ranges.kp.min, impl_->ranges.kp.max, 12);
    uint16_t kd_int = FloatToUint(kd, impl_->ranges.kd.min, impl_->ranges.kd.max, 9);
    uint16_t pos_int = FloatToUint(pos, impl_->ranges.position.min, impl_->ranges.position.max, 16);
    uint16_t spd_int = FloatToUint(spd, impl_->ranges.speed.min, impl_->ranges.speed.max, 12);
    uint16_t tor_int = FloatToUint(torque, impl_->ranges.torque.min, impl_->ranges.torque.max, 12);

    msg.data[0] = static_cast<uint8_t>(kp_int >> 7);
    msg.data[1] = static_cast<uint8_t>(((kp_int & 0x7F) << 1) | ((kd_int & 0x100) >> 8));
    msg.data[2] = static_cast<uint8_t>(kd_int & 0xFF);
    msg.data[3] = static_cast<uint8_t>(pos_int >> 8);
    msg.data[4] = static_cast<uint8_t>(pos_int & 0xFF);
    msg.data[5] = static_cast<uint8_t>(spd_int >> 4);
    msg.data[6] = static_cast<uint8_t>(((spd_int & 0x0F) << 4) | (tor_int >> 8));
    msg.data[7] = static_cast<uint8_t>(tor_int & 0xFF);

    std::optional<MotorPackMsg> response;
    if constexpr (FeedbackType != 0) {
        const auto current_range = impl_->current_range.load();
        response = SendAndWait(msg, [idx = impl_->idx.load(std::memory_order_relaxed),
                                     current_range](const MotorPackMsg& pack) {
            return pack.id == idx && DecodeFeedback<1>(pack, current_range).has_value();
        });
    } else {
        SendMessageLocked(msg);
    }
    RecordCommand("PVTControl", kp, kd, pos, spd, std::nullopt, torque, std::nullopt, std::nullopt,
                  FeedbackType);

    if constexpr (FeedbackType == 0) {
        return;
    } else {
        auto res =
            response ? DecodeFeedback<1>(*response, impl_->current_range.load()) : std::nullopt;
        if (res) {
            return *res;
        } else {
            return {};
        }
    }
}
template <int FeedbackType>
auto Motor::PosControl(float position, float speed, float current, int feedback)
    -> std::conditional_t<FeedbackType == 0, void, FeedbackStruct<FeedbackType>> {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    if constexpr (FeedbackType != 0) {
        if (FeedbackType != feedback) {
            throw std::invalid_argument(
                "FeedbackType template parameter must match feedback argument if not zero");
        }
    }

    static_assert(FeedbackType == 0 || FeedbackType == 1 || FeedbackType == 2 || FeedbackType == 3,
                  "Invalid FeedbackType: must be 0, 1, 2 or 3");
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus) {
        if constexpr (FeedbackType == 0)
            return;
        else
            return FeedbackStruct<FeedbackType>{};
    }

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 8;

    speed = speed * 30.f / M_PIf;
    const float logged_speed = std::clamp(speed, 0.0F, 3276.7F) * M_PIf / 30.0F;
    const float logged_current = std::clamp(current, 0.0F, 409.5F);
    const std::uint32_t position_bits = FloatBits(position * 180 / M_PIf);
    uint16_t spd_int = FloatToUint(speed, 0, 3276.7f, 15);
    uint16_t cur_int = FloatToUint(current, 0, 409.5f, 12);
    const auto position_byte = [position_bits](int shift) {
        return static_cast<uint8_t>(position_bits >> shift);
    };
    msg.data[0] = static_cast<uint8_t>(0x20 | (position_byte(24) >> 3));
    msg.data[1] = static_cast<uint8_t>((position_byte(24) << 5) | (position_byte(16) >> 3));
    msg.data[2] = static_cast<uint8_t>((position_byte(16) << 5) | (position_byte(8) >> 3));
    msg.data[3] = static_cast<uint8_t>((position_byte(8) << 5) | (position_byte(0) >> 3));
    msg.data[4] = static_cast<uint8_t>((position_byte(0) << 5) | (spd_int >> 10));
    msg.data[5] = static_cast<uint8_t>((spd_int & 0x3FC) >> 2);
    msg.data[6] = static_cast<uint8_t>(((spd_int & 0x03) << 6) | (cur_int >> 6));
    msg.data[7] = static_cast<uint8_t>(((cur_int & 0x3F) << 2) | feedback);
    std::optional<MotorPackMsg> response;
    if constexpr (FeedbackType != 0) {
        const auto current_range = impl_->current_range.load();
        response = SendAndWait(msg, [idx = impl_->idx.load(std::memory_order_relaxed),
                                     current_range](const MotorPackMsg& pack) {
            return pack.id == idx && DecodeFeedback<FeedbackType>(pack, current_range).has_value();
        });
    } else {
        SendMessageLocked(msg);
    }
    RecordCommand("PosControl", std::nullopt, std::nullopt, position, logged_speed, logged_current,
                  std::nullopt, std::nullopt, std::nullopt, feedback);

    if constexpr (FeedbackType == 0) {
        return;
    } else {
        auto res = response ? DecodeFeedback<FeedbackType>(*response, impl_->current_range.load())
                            : std::nullopt;
        if (res)
            return *res;
        return FeedbackStruct<FeedbackType>{};
    }
}
template <int FeedbackType>
auto Motor::SpdControl(float speed, float current, int feedback)
    -> std::conditional_t<FeedbackType == 0, void, FeedbackStruct<FeedbackType>> {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    if constexpr (FeedbackType != 0) {
        if (FeedbackType != feedback) {
            throw std::invalid_argument(
                "FeedbackType template parameter must match feedback argument if not zero");
        }
    }

    static_assert(FeedbackType == 0 || FeedbackType == 1 || FeedbackType == 2 || FeedbackType == 3,
                  "Invalid FeedbackType: must be 0, 1, 2 or 3");
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus) {
        if constexpr (FeedbackType == 0)
            return;
        else
            return FeedbackStruct<FeedbackType>{};
    }

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 7;

    const std::uint32_t speed_bits = FloatBits(speed * kRadiansPerSecondToRpm);
    uint16_t cur = static_cast<uint16_t>(current * 10);
    msg.data[0] = static_cast<uint8_t>(0x40 | feedback);
    msg.data[1] = static_cast<uint8_t>(speed_bits >> 24);
    msg.data[2] = static_cast<uint8_t>(speed_bits >> 16);
    msg.data[3] = static_cast<uint8_t>(speed_bits >> 8);
    msg.data[4] = static_cast<uint8_t>(speed_bits);
    msg.data[5] = static_cast<uint8_t>(cur >> 8);
    msg.data[6] = static_cast<uint8_t>(cur & 0xFF);

    std::optional<MotorPackMsg> response;
    if constexpr (FeedbackType != 0) {
        const auto current_range = impl_->current_range.load();
        response = SendAndWait(msg, [idx = impl_->idx.load(std::memory_order_relaxed),
                                     current_range](const MotorPackMsg& pack) {
            return pack.id == idx && DecodeFeedback<FeedbackType>(pack, current_range).has_value();
        });
    } else {
        SendMessageLocked(msg);
    }
    RecordCommand("SpdControl", std::nullopt, std::nullopt, std::nullopt, speed, current,
                  std::nullopt, std::nullopt, std::nullopt, feedback);

    if constexpr (FeedbackType == 0) {
        return;
    } else {
        auto res = response ? DecodeFeedback<FeedbackType>(*response, impl_->current_range.load())
                            : std::nullopt;
        if (res)
            return *res;
        return FeedbackStruct<FeedbackType>{};
    }
}
template <int FeedbackType>
auto Motor::CurControl(float current, int feedback)
    -> std::conditional_t<FeedbackType == 0, void, FeedbackStruct<FeedbackType>> {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    if constexpr (FeedbackType != 0) {
        if (FeedbackType != feedback) {
            throw std::invalid_argument(
                "FeedbackType template parameter must match feedback argument if not zero");
        }
    }

    static_assert(FeedbackType == 0 || FeedbackType == 1 || FeedbackType == 2 || FeedbackType == 3,
                  "Invalid FeedbackType: must be 0, 1, 2 or 3");
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus) {
        if constexpr (FeedbackType == 0)
            return;
        else
            return FeedbackStruct<FeedbackType>{};
    }

    int16_t cur = static_cast<int16_t>(current * 100.f);

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 3;
    msg.data[0] = static_cast<uint8_t>(0x60 | (0 << 2) | feedback);
    msg.data[1] = static_cast<uint8_t>(cur >> 8);
    msg.data[2] = static_cast<uint8_t>(cur & 0xFF);

    std::optional<MotorPackMsg> response;
    if constexpr (FeedbackType != 0) {
        const auto current_range = impl_->current_range.load();
        response = SendAndWait(msg, [idx = impl_->idx.load(std::memory_order_relaxed),
                                     current_range](const MotorPackMsg& pack) {
            return pack.id == idx && DecodeFeedback<FeedbackType>(pack, current_range).has_value();
        });
    } else {
        SendMessageLocked(msg);
    }
    RecordCommand("CurControl", std::nullopt, std::nullopt, std::nullopt, std::nullopt, current,
                  std::nullopt, std::nullopt, std::nullopt, feedback);

    if constexpr (FeedbackType == 0) {
        return;
    } else {
        auto res = response ? DecodeFeedback<FeedbackType>(*response, impl_->current_range.load())
                            : std::nullopt;
        if (res)
            return *res;
        return FeedbackStruct<FeedbackType>{};
    }
}
template <int FeedbackType>
auto Motor::TorControl(float torque, int feedback)
    -> std::conditional_t<FeedbackType == 0, void, FeedbackStruct<FeedbackType>> {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    if constexpr (FeedbackType != 0) {
        if (FeedbackType != feedback) {
            throw std::invalid_argument(
                "FeedbackType template parameter must match feedback argument if not zero");
        }
    }

    static_assert(FeedbackType == 0 || FeedbackType == 1 || FeedbackType == 2 || FeedbackType == 3,
                  "Invalid FeedbackType: must be 0, 1, 2 or 3");
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus) {
        if constexpr (FeedbackType == 0)
            return;
        else
            return FeedbackStruct<FeedbackType>{};
    }

    int16_t tor = static_cast<int16_t>(torque * 100.f);

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 3;
    msg.data[0] = static_cast<uint8_t>(0x60 | (1 << 2) | feedback);
    msg.data[1] = static_cast<uint8_t>(tor >> 8);
    msg.data[2] = static_cast<uint8_t>(tor & 0xFF);

    std::optional<MotorPackMsg> response;
    if constexpr (FeedbackType != 0) {
        const auto current_range = impl_->current_range.load();
        response = SendAndWait(msg, [idx = impl_->idx.load(std::memory_order_relaxed),
                                     current_range](const MotorPackMsg& pack) {
            return pack.id == idx && DecodeFeedback<FeedbackType>(pack, current_range).has_value();
        });
    } else {
        SendMessageLocked(msg);
    }
    RecordCommand("TorControl", std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                  std::nullopt, torque, std::nullopt, std::nullopt, feedback);

    if constexpr (FeedbackType == 0) {
        return;
    } else {
        auto res = response ? DecodeFeedback<FeedbackType>(*response, impl_->current_range.load())
                            : std::nullopt;
        if (res)
            return *res;
        return FeedbackStruct<FeedbackType>{};
    }
}
template <int FeedbackType>
auto Motor::Stop(MotorStopMode mode, float current, int feedback)
    -> std::conditional_t<FeedbackType == 0, void, FeedbackStruct<FeedbackType>> {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    static_assert(FeedbackType == 0 || FeedbackType == 1 || FeedbackType == 2 || FeedbackType == 3,
                  "Invalid FeedbackType: must be 0, 1, 2 or 3");
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus) {
        if constexpr (FeedbackType == 0)
            return;
        else
            return FeedbackStruct<FeedbackType>{};
    }

    int16_t cur = static_cast<int16_t>(current * 100.f);

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 3;
    msg.data[0] = static_cast<uint8_t>(0x03 << 5 | (static_cast<uint8_t>(mode) << 2) | feedback);
    msg.data[1] = static_cast<uint8_t>(cur >> 8);
    msg.data[2] = static_cast<uint8_t>(cur & 0xFF);

    std::optional<MotorPackMsg> response;
    if constexpr (FeedbackType != 0) {
        const auto current_range = impl_->current_range.load();
        response = SendAndWait(msg, [idx = impl_->idx.load(std::memory_order_relaxed),
                                     current_range](const MotorPackMsg& pack) {
            return pack.id == idx && DecodeFeedback<FeedbackType>(pack, current_range).has_value();
        });
    } else {
        SendMessageLocked(msg);
    }
    RecordCommand("Stop", std::nullopt, std::nullopt, std::nullopt, std::nullopt, current,
                  std::nullopt, mode, std::nullopt, feedback);

    if constexpr (FeedbackType == 0) {
        return;
    } else {
        auto res = response ? DecodeFeedback<FeedbackType>(*response, impl_->current_range.load())
                            : std::nullopt;
        if (res)
            return *res;
        return FeedbackStruct<FeedbackType>{};
    }
}
bool Motor::Brake(bool enabled, bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 3;
    msg.data[0] = static_cast<uint8_t>((0x04 << 5));
    msg.data[1] = static_cast<uint8_t>(enabled ? 1 : 0);

    RecordCommand("Brake", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                  std::nullopt, std::nullopt, enabled, std::nullopt);

    if (!wait_for_ack) {
        SendMessageLocked(msg);
        return true;
    }

    const auto ack = SendAndWait(msg, [enabled](const MotorPackMsg& pack) {
        return packet_has_payload(pack, 0, 2) && pack.data[0] == 0xb2 &&
               pack.data[1] == static_cast<uint8_t>(enabled ? 1 : 0);
    });
    return ack.has_value();
}
// Explicit template instantiations for common feedback types
template void Motor::PVTControl<0>(float, float, float, float, float);
template MotorFeedbackMsg1 Motor::PVTControl<1>(float, float, float, float, float);

template void Motor::PosControl<0>(float, float, float, int);
template MotorFeedbackMsg1 Motor::PosControl<1>(float, float, float, int);
template MotorFeedbackMsg2 Motor::PosControl<2>(float, float, float, int);
template MotorFeedbackMsg3 Motor::PosControl<3>(float, float, float, int);

template void Motor::SpdControl<0>(float, float, int);
template MotorFeedbackMsg1 Motor::SpdControl<1>(float, float, int);
template MotorFeedbackMsg2 Motor::SpdControl<2>(float, float, int);
template MotorFeedbackMsg3 Motor::SpdControl<3>(float, float, int);

template void Motor::CurControl<0>(float, int);
template MotorFeedbackMsg1 Motor::CurControl<1>(float, int);
template MotorFeedbackMsg2 Motor::CurControl<2>(float, int);
template MotorFeedbackMsg3 Motor::CurControl<3>(float, int);

template void Motor::TorControl<0>(float, int);
template MotorFeedbackMsg1 Motor::TorControl<1>(float, int);
template MotorFeedbackMsg2 Motor::TorControl<2>(float, int);
template MotorFeedbackMsg3 Motor::TorControl<3>(float, int);

template void Motor::Stop<0>(MotorStopMode, float, int);
template MotorFeedbackMsg1 Motor::Stop<1>(MotorStopMode, float, int);
template MotorFeedbackMsg2 Motor::Stop<2>(MotorStopMode, float, int);
template MotorFeedbackMsg3 Motor::Stop<3>(MotorStopMode, float, int);

}  // namespace encos
