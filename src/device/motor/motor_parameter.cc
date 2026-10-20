// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

#include "bus/bus_impl.h"
#include "device/motor/math_utils.h"
#include "device/motor/motor.h"
#include "device/motor/motor_impl.h"
#include "device/motor/pack_helper.h"
#include "encos/driver_manager.h"
#include "platform/wait.h"
#include "utils/scope_exit.h"

namespace encos {

namespace {

template <typename>
struct AlwaysFalse : std::false_type {};

/**
 * @brief 速度环/位置环增益的倍率倒数（`原始值 = 参数值 × 本值`）
 * @param fw_version 电机固件版本，0 表示旧版固件
 * @return 旧版固件为 1e5（参数值 = 原始值 × 0.00001），新版固件为 500（参数值 = 原始值 × 0.002）
 */
float LoopGainScaleFor(uint32_t fw_version) {
    return fw_version == 0 ? 100000.f : 500.f;
}

template <MotorParameter Param>
constexpr std::size_t ParameterPayloadSize() {
    using RawType = MotorParameterRawType<Param>;
    if constexpr (std::is_same_v<RawType, float> || std::is_same_v<RawType, int> ||
                  std::is_same_v<RawType, uint32_t>) {
        return 4;
    } else if constexpr (std::is_same_v<RawType, uint8_t>) {
        return 1;
    } else if constexpr (std::is_same_v<RawType, uint16_t>) {
        return 2;
    } else if constexpr (std::is_same_v<RawType, Range<uint16_t>> ||
                         std::is_same_v<RawType, Range<int16_t>> ||
                         std::is_same_v<RawType, KpKi<uint16_t>> ||
                         std::is_same_v<RawType, KpKd<uint16_t>>) {
        return 4;
    } else if constexpr (std::is_same_v<RawType, MotorEncoderAngle>) {
        return 5;
    } else {
        static_assert(AlwaysFalse<RawType>::value, "Unsupported motor parameter raw type");
    }
}

template <MotorParameter Param>
MotorParameterRawType<Param> DecodeParameterRaw(const MotorPackMsg& pack) {
    using RawType = MotorParameterRawType<Param>;
    const auto* payload = pack.data + 2;

    if constexpr (std::is_same_v<RawType, float>) {
        return read_float_be(payload);
    } else if constexpr (std::is_same_v<RawType, uint8_t>) {
        return payload[0];
    } else if constexpr (std::is_same_v<RawType, uint16_t>) {
        return read_u16_be(payload);
    } else if constexpr (std::is_same_v<RawType, uint32_t>) {
        return read_u32_be(payload);
    } else if constexpr (std::is_same_v<RawType, int>) {
        return static_cast<int>(read_i32_be(payload));
    } else if constexpr (std::is_same_v<RawType, Range<uint16_t>>) {
        return {read_u16_be(payload), read_u16_be(payload + 2)};
    } else if constexpr (std::is_same_v<RawType, Range<int16_t>>) {
        return {read_i16_be(payload), read_i16_be(payload + 2)};
    } else if constexpr (std::is_same_v<RawType, MotorEncoderAngle>) {
        MotorEncoderAngle angle{};
        angle.cal_flag = payload[0];
        angle.raw_angle = static_cast<float>(read_i16_be(payload + 1)) * 0.01f;
        angle.cal_angle = static_cast<float>(read_i16_be(payload + 3)) * 0.01f;
        return angle;
    } else if constexpr (std::is_same_v<RawType, KpKi<uint16_t>>) {
        return {read_u16_be(payload), read_u16_be(payload + 2)};
    } else if constexpr (std::is_same_v<RawType, KpKd<uint16_t>>) {
        return {read_u16_be(payload), read_u16_be(payload + 2)};
    } else {
        static_assert(AlwaysFalse<RawType>::value, "Unsupported motor parameter raw type");
    }
}

}  // namespace

void Motor::InitMotorPVTParam() {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    auto kp_range = GetParameter<MotorParameter::PVTKpRange>();
    auto kd_range = GetParameter<MotorParameter::PVTKdRange>();
    auto pos_range = GetParameter<MotorParameter::PVTPosRange>();
    auto spd_range = GetParameter<MotorParameter::PVTSpdRange>();
    auto tor_range = GetParameter<MotorParameter::PVTTorRange>();
    auto cur_range = GetParameter<MotorParameter::PVTCurRange>();
    auto kt = GetParameter<MotorParameter::Kt>();
    platform::LockGuard<platform::Mutex> ranges_lock(impl_->feedback_ranges_mutex);
    impl_->ranges.kp = {static_cast<float>(kp_range.min), static_cast<float>(kp_range.max)};
    impl_->ranges.kd = {static_cast<float>(kd_range.min), static_cast<float>(kd_range.max)};
    impl_->ranges.position = {pos_range.min, pos_range.max};
    impl_->ranges.speed = {spd_range.min, spd_range.max};
    impl_->ranges.torque = {tor_range.min, tor_range.max};
    impl_->ranges.current = {cur_range.min, cur_range.max};
    impl_->ranges.kt = kt;
}
void Motor::QueryFirmwareVersion() {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);

    uint32_t version = 0;
    if (impl_->bus) {
        MotorPackMsg msg{};
        msg.id = impl_->idx.load(std::memory_order_relaxed);
        msg.len = 2;
        msg.data[0] = static_cast<uint8_t>(0x07 << 5);
        msg.data[1] = static_cast<uint8_t>(MotorParameter::FirmwareVersion);
        // 只等一次且用短超时：旧版固件不响应 0x32，GetParameter 的重试会白等两个完整超时。
        const auto packet = SendAndWait(
            msg,
            [](const MotorPackMsg& pack) {
                return pack.data[0] == static_cast<uint8_t>(0x05 << 5) &&
                       pack.data[1] == static_cast<uint8_t>(MotorParameter::FirmwareVersion) &&
                       packet_has_payload(pack, 2, 4);
            },
            kFirmwareProbeTimeout);
        if (packet) {
            version = read_u32_be(packet->data + 2);
        }
    }

    platform::LockGuard<platform::Mutex> ranges_lock(impl_->feedback_ranges_mutex);
    impl_->ranges.fw_version = version;
}
bool Motor::SetId(uint16_t new_idx, bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = 0x7FF;
    msg.len = 6;
    const auto motor_idx = impl_->idx.load(std::memory_order_relaxed);
    msg.data[0] = static_cast<uint8_t>(motor_idx >> 8);
    msg.data[1] = static_cast<uint8_t>(motor_idx & 0xFF);
    msg.data[2] = 0x00;
    msg.data[3] = 0x04;
    msg.data[4] = static_cast<uint8_t>(new_idx >> 8);
    msg.data[5] = static_cast<uint8_t>(new_idx & 0xFF);

    if (!wait_for_ack) {
        SendMessageLocked(msg);
        return true;
    }

    auto& manager = EncosDriverManager::Instance();
    if (!manager.ReserveMotorIndex(this, new_idx)) {
        return false;
    }
    const auto reservation = utils::MakeScopeExit([this, &manager, new_idx] {
        manager.ReleaseMotorIndexReservation(this, new_idx);
    });
    const auto res = SendAndWait(msg, [new_idx](const MotorPackMsg& pack) {
        return pack.data[0] == static_cast<uint8_t>(new_idx >> 8) &&
               pack.data[1] == static_cast<uint8_t>(new_idx & 0xFF) && pack.data[3] == 0x04;
    });
    const auto success = res.has_value();
    if (!success) {
        return false;
    }
    if (!manager.MigrateMotorIndex(this, new_idx)) {
        return false;
    }
    return true;
}
bool Motor::SetPos(double now_pos) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    const double centidegrees = now_pos * 180.0 / M_PI * 100.0;
    if (centidegrees < static_cast<double>(std::numeric_limits<int16_t>::min()) ||
        centidegrees > static_cast<double>(std::numeric_limits<int16_t>::max())) {
        throw std::out_of_range("SetPos position exceeds int16 centidegree range");
    }
    const int16_t position = static_cast<int16_t>(centidegrees);

    MotorPackMsg msg{};
    msg.id = 0x7FF;
    msg.len = 6;
    const auto motor_idx = impl_->idx.load(std::memory_order_relaxed);
    msg.data[0] = static_cast<uint8_t>(motor_idx >> 8);
    msg.data[1] = static_cast<uint8_t>(motor_idx & 0xFF);
    msg.data[2] = 0x00;
    msg.data[3] = 0x03;
    msg.data[4] = static_cast<uint8_t>(position >> 8);
    msg.data[5] = static_cast<uint8_t>(position & 0xFF);

    const auto res = SendAndWait(
        msg,
        [this](const MotorPackMsg& pack) {
            const auto motor_idx = impl_->idx.load(std::memory_order_relaxed);
            return pack.data[0] == static_cast<uint8_t>(motor_idx >> 8) &&
                   pack.data[1] == static_cast<uint8_t>(motor_idx & 0xFF) && pack.data[3] == 0x03;
        },
        std::chrono::milliseconds(1000));
    return res.has_value();
}
bool Motor::ResetZeroPos(bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = 0x7FF;
    msg.len = 4;
    const auto motor_idx = impl_->idx.load(std::memory_order_relaxed);
    msg.data[0] = static_cast<uint8_t>(motor_idx >> 8);
    msg.data[1] = static_cast<uint8_t>(motor_idx & 0xFF);
    msg.data[2] = 0x00;
    msg.data[3] = 0x03;

    if (!wait_for_ack) {
        SendMessageLocked(msg);
        return true;
    }

    const auto res = SendAndWait(
        msg,
        [this](const MotorPackMsg& pack) {
            const auto motor_idx = impl_->idx.load(std::memory_order_relaxed);
            return pack.data[0] == static_cast<uint8_t>(motor_idx >> 8) &&
                   pack.data[1] == static_cast<uint8_t>(motor_idx & 0xFF) && pack.data[3] == 0x03;
        },
        std::chrono::milliseconds(1000));
    return res.has_value();
}
bool Motor::SetPVTKpRange(Range<uint16_t> kp_range, bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 6;
    msg.data[0] = static_cast<uint8_t>((0x06 << 5));
    msg.data[1] = 0x05;
    msg.data[2] = static_cast<uint8_t>(kp_range.min >> 8);
    msg.data[3] = static_cast<uint8_t>(kp_range.min & 0xFF);
    msg.data[4] = static_cast<uint8_t>(kp_range.max >> 8);
    msg.data[5] = static_cast<uint8_t>(kp_range.max & 0xFF);

    bool success = false;
    if (wait_for_ack) {
        success = SendParameterAndWait(
            msg, 0x05,
            {static_cast<uint8_t>(kp_range.min >> 8), static_cast<uint8_t>(kp_range.min & 0xFF),
             static_cast<uint8_t>(kp_range.max >> 8), static_cast<uint8_t>(kp_range.max & 0xFF)});
    } else {
        SendMessageLocked(msg);
        success = true;
    }

    if (success) {
        impl_->ranges.kp = {static_cast<float>(kp_range.min), static_cast<float>(kp_range.max)};
    }

    return success;
}
bool Motor::SetPVTKdRange(Range<uint16_t> kd_range, bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 6;
    msg.data[0] = static_cast<uint8_t>((0x06 << 5));
    msg.data[1] = 0x06;
    msg.data[2] = static_cast<uint8_t>(kd_range.min >> 8);
    msg.data[3] = static_cast<uint8_t>(kd_range.min & 0xFF);
    msg.data[4] = static_cast<uint8_t>(kd_range.max >> 8);
    msg.data[5] = static_cast<uint8_t>(kd_range.max & 0xFF);

    bool success = false;
    if (wait_for_ack) {
        success = SendParameterAndWait(
            msg, 0x06,
            {static_cast<uint8_t>(kd_range.min >> 8), static_cast<uint8_t>(kd_range.min & 0xFF),
             static_cast<uint8_t>(kd_range.max >> 8), static_cast<uint8_t>(kd_range.max & 0xFF)});
    } else {
        SendMessageLocked(msg);
        success = true;
    }

    if (success) {
        impl_->ranges.kd = {static_cast<float>(kd_range.min), static_cast<float>(kd_range.max)};
    }

    return success;
}
bool Motor::SetPVTPosRange(Range<float> pos_range, bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 6;
    msg.data[0] = static_cast<uint8_t>((0x06 << 5));
    msg.data[1] = 0x07;
    int16_t min_pos = static_cast<int16_t>(pos_range.min * 100.0f);
    int16_t max_pos = static_cast<int16_t>(pos_range.max * 100.0f);
    msg.data[2] = static_cast<uint8_t>(min_pos >> 8);
    msg.data[3] = static_cast<uint8_t>(min_pos & 0xFF);
    msg.data[4] = static_cast<uint8_t>(max_pos >> 8);
    msg.data[5] = static_cast<uint8_t>(max_pos & 0xFF);
    bool success = false;
    if (wait_for_ack) {
        success = SendParameterAndWait(
            msg, 0x07,
            {static_cast<uint8_t>(min_pos >> 8), static_cast<uint8_t>(min_pos & 0xFF),
             static_cast<uint8_t>(max_pos >> 8), static_cast<uint8_t>(max_pos & 0xFF)});
    } else {
        SendMessageLocked(msg);
        success = true;
    }

    if (success) {
        platform::LockGuard<platform::Mutex> ranges_lock(impl_->feedback_ranges_mutex);
        impl_->ranges.position = {static_cast<float>(min_pos) / 100.0f,
                                  static_cast<float>(max_pos) / 100.0f};
    }

    return success;
}
bool Motor::SetPVTSpdRange(Range<float> spd_range, bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 6;
    msg.data[0] = static_cast<uint8_t>((0x06 << 5));
    msg.data[1] = 0x08;
    int16_t min_spd = static_cast<int16_t>(spd_range.min * 100.0f);
    int16_t max_spd = static_cast<int16_t>(spd_range.max * 100.0f);
    msg.data[2] = static_cast<uint8_t>(min_spd >> 8);
    msg.data[3] = static_cast<uint8_t>(min_spd & 0xFF);
    msg.data[4] = static_cast<uint8_t>(max_spd >> 8);
    msg.data[5] = static_cast<uint8_t>(max_spd & 0xFF);
    bool success = false;
    if (wait_for_ack) {
        success = SendParameterAndWait(
            msg, 0x08,
            {static_cast<uint8_t>(min_spd >> 8), static_cast<uint8_t>(min_spd & 0xFF),
             static_cast<uint8_t>(max_spd >> 8), static_cast<uint8_t>(max_spd & 0xFF)});
    } else {
        SendMessageLocked(msg);
        success = true;
    }

    if (success) {
        platform::LockGuard<platform::Mutex> ranges_lock(impl_->feedback_ranges_mutex);
        impl_->ranges.speed = {static_cast<float>(min_spd) / 100.0f,
                               static_cast<float>(max_spd) / 100.0f};
    }

    return success;
}
bool Motor::SetPVTTorRange(Range<float> tor_range, bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 6;
    msg.data[0] = static_cast<uint8_t>((0x06 << 5));
    msg.data[1] = 0x09;
    int16_t min_tor = static_cast<int16_t>(tor_range.min * 10.0f);
    int16_t max_tor = static_cast<int16_t>(tor_range.max * 10.0f);
    msg.data[2] = static_cast<uint8_t>(min_tor >> 8);
    msg.data[3] = static_cast<uint8_t>(min_tor & 0xFF);
    msg.data[4] = static_cast<uint8_t>(max_tor >> 8);
    msg.data[5] = static_cast<uint8_t>(max_tor & 0xFF);
    bool success = false;
    if (wait_for_ack) {
        success = SendParameterAndWait(
            msg, 0x09,
            {static_cast<uint8_t>(min_tor >> 8), static_cast<uint8_t>(min_tor & 0xFF),
             static_cast<uint8_t>(max_tor >> 8), static_cast<uint8_t>(max_tor & 0xFF)});
    } else {
        SendMessageLocked(msg);
        success = true;
    }

    if (success) {
        impl_->ranges.torque = {static_cast<float>(min_tor) / 10.0f,
                                static_cast<float>(max_tor) / 10.0f};
    }

    return success;
}
bool Motor::SetPVTCurRange(Range<float> cur_range, bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 6;
    msg.data[0] = static_cast<uint8_t>((0x06 << 5));
    msg.data[1] = 0x0a;
    int16_t min_cur = static_cast<int16_t>(cur_range.min * 10.0f);
    int16_t max_cur = static_cast<int16_t>(cur_range.max * 10.0f);
    msg.data[2] = static_cast<uint8_t>(min_cur >> 8);
    msg.data[3] = static_cast<uint8_t>(min_cur & 0xFF);
    msg.data[4] = static_cast<uint8_t>(max_cur >> 8);
    msg.data[5] = static_cast<uint8_t>(max_cur & 0xFF);
    bool success = false;
    if (wait_for_ack) {
        success = SendParameterAndWait(
            msg, 0x0a,
            {static_cast<uint8_t>(min_cur >> 8), static_cast<uint8_t>(min_cur & 0xFF),
             static_cast<uint8_t>(max_cur >> 8), static_cast<uint8_t>(max_cur & 0xFF)});
    } else {
        SendMessageLocked(msg);
        success = true;
    }

    if (success) {
        platform::LockGuard<platform::Mutex> ranges_lock(impl_->feedback_ranges_mutex);
        impl_->ranges.current = {static_cast<float>(min_cur) / 10.0f,
                                 static_cast<float>(max_cur) / 10.0f};
    }

    return success;
}
bool Motor::SetCanTimeout(uint16_t timeout_ms, bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 4;
    msg.data[0] = static_cast<uint8_t>((0x06 << 5));
    msg.data[1] = 0x0b;
    msg.data[2] = static_cast<uint8_t>(timeout_ms >> 8);
    msg.data[3] = static_cast<uint8_t>(timeout_ms & 0xFF);

    if (!wait_for_ack) {
        SendMessageLocked(msg);
        return true;
    }

    return SendParameterAndWait(
        msg, 0x0b,
        {static_cast<uint8_t>(timeout_ms >> 8), static_cast<uint8_t>(timeout_ms & 0xFF)});
}
bool Motor::SetCommunicationMode(MotorCommunicationMode mode, bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    const auto mode_value = static_cast<uint8_t>(mode);
    if (mode_value > static_cast<uint8_t>(MotorCommunicationMode::CanOpen)) {
        throw std::invalid_argument("Invalid motor communication mode");
    }

    if (impl_->ranges.fw_version != 0) {
        // 新版协议移除了配置通信模式（0x02），发送不会被响应，直接判定为不支持且不产生报文。
        if (impl_->logger_) {
            impl_->logger_->warn(
                "Motor {} firmware has no communication mode command, ignoring "
                "SetCommunicationMode({})",
                impl_->idx.load(std::memory_order_relaxed), mode_value);
        }
        return false;
    }

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 3;
    msg.data[0] =
        static_cast<uint8_t>((0x06 << 5) | (0 << 2) | static_cast<uint8_t>(wait_for_ack ? 1 : 0));
    msg.data[1] = 0x02;
    msg.data[2] = mode_value;

    if (!wait_for_ack) {
        SendMessageLocked(msg);
        return true;
    }

    return SendParameterAndWait(msg, 0x02, {mode_value});
}
bool Motor::SetCurPI(float kp, float ki, bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 6;

    uint16_t kp_int = static_cast<uint16_t>(kp * 10000.f);
    uint16_t ki_int = static_cast<uint16_t>(ki * 10.f);

    msg.data[0] = static_cast<uint8_t>((0x06 << 5));
    msg.data[1] = 0x0c;
    msg.data[2] = static_cast<uint8_t>(kp_int >> 8);
    msg.data[3] = static_cast<uint8_t>(kp_int & 0xFF);
    msg.data[4] = static_cast<uint8_t>(ki_int >> 8);
    msg.data[5] = static_cast<uint8_t>(ki_int & 0xFF);

    if (!wait_for_ack) {
        SendMessageLocked(msg);
        return true;
    }

    return SendParameterAndWait(
        msg, 0x0c,
        {static_cast<uint8_t>(kp_int >> 8), static_cast<uint8_t>(kp_int & 0xFF),
         static_cast<uint8_t>(ki_int >> 8), static_cast<uint8_t>(ki_int & 0xFF)});
}
bool Motor::SetSpdPI(float kp, float ki, bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 6;

    const float gain_scale = LoopGainScaleFor(impl_->ranges.fw_version);
    uint16_t kp_int = static_cast<uint16_t>(std::clamp(kp * gain_scale, 0.0f, 65535.0f));
    uint16_t ki_int = static_cast<uint16_t>(std::clamp(ki * gain_scale, 0.0f, 65535.0f));

    msg.data[0] = static_cast<uint8_t>((0x06 << 5));
    msg.data[1] = 0x0d;
    msg.data[2] = static_cast<uint8_t>(kp_int >> 8);
    msg.data[3] = static_cast<uint8_t>(kp_int & 0xFF);
    msg.data[4] = static_cast<uint8_t>(ki_int >> 8);
    msg.data[5] = static_cast<uint8_t>(ki_int & 0xFF);

    if (!wait_for_ack) {
        SendMessageLocked(msg);
        return true;
    }

    return SendParameterAndWait(
        msg, 0x0d,
        {static_cast<uint8_t>(kp_int >> 8), static_cast<uint8_t>(kp_int & 0xFF),
         static_cast<uint8_t>(ki_int >> 8), static_cast<uint8_t>(ki_int & 0xFF)});
}
bool Motor::SetPosPD(float kp, float kd, bool wait_for_ack) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 6;

    const float gain_scale = LoopGainScaleFor(impl_->ranges.fw_version);
    uint16_t kp_int = static_cast<uint16_t>(std::clamp(kp * gain_scale, 0.0f, 65535.0f));
    uint16_t kd_int = static_cast<uint16_t>(std::clamp(kd * gain_scale, 0.0f, 65535.0f));

    msg.data[0] = static_cast<uint8_t>((0x06 << 5));
    msg.data[1] = 0x0e;
    msg.data[2] = static_cast<uint8_t>(kp_int >> 8);
    msg.data[3] = static_cast<uint8_t>(kp_int & 0xFF);
    msg.data[4] = static_cast<uint8_t>(kd_int >> 8);
    msg.data[5] = static_cast<uint8_t>(kd_int & 0xFF);
    if (!wait_for_ack) {
        SendMessageLocked(msg);
        return true;
    }

    return SendParameterAndWait(
        msg, 0x0e,
        {static_cast<uint8_t>(kp_int >> 8), static_cast<uint8_t>(kp_int & 0xFF),
         static_cast<uint8_t>(kd_int >> 8), static_cast<uint8_t>(kd_int & 0xFF)});
}
template <MotorParameter Param>
auto Motor::GetParameter() -> MotorParameterRetType<Param> {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::RecursiveMutex> lock(impl_->motor_mutex);
    using RawType = MotorParameterRawType<Param>;
    using RetType = MotorParameterRetType<Param>;
    if (!impl_->bus)
        throw std::runtime_error("Bus not initialized");

    MotorPackMsg msg{};
    msg.id = impl_->idx.load(std::memory_order_relaxed);
    msg.len = 2;
    msg.data[0] = static_cast<uint8_t>((0x07 << 5));
    msg.data[1] = static_cast<uint8_t>(Param);

    auto checker = [](const MotorPackMsg& pack) {
        return pack.data[0] == static_cast<uint8_t>(0x05 << 5) &&
               pack.data[1] == static_cast<uint8_t>(Param) &&
               packet_has_payload(pack, 2, ParameterPayloadSize<Param>());
    };

    auto packet = SendAndWait(msg, checker);
    if (!packet) {
        packet = SendAndWait(msg, checker);
    }

    if (!packet) {
        throw std::runtime_error("Get motor parameter timeout");
    }
    RawType res = DecodeParameterRaw<Param>(*packet);

    if constexpr (Param == MotorParameter::Position) {
        res = res / 180.f * M_PIf;
    }
    if constexpr (Param == MotorParameter::Speed) {
        res = res / 30.f * M_PIf;
    }

    if constexpr (std::is_same_v<RetType, RawType>) {
        return res;
    } else {
        if constexpr (Param == MotorParameter::Kt) {
            return static_cast<float>(res) / 100.0f;
        } else if constexpr (Param == MotorParameter::PVTPosRange ||
                             Param == MotorParameter::PVTSpdRange) {
            return {static_cast<float>(res.min) / 100.0f, static_cast<float>(res.max) / 100.0f};
        } else if constexpr (Param == MotorParameter::PVTTorRange ||
                             Param == MotorParameter::PVTCurRange) {
            return {static_cast<float>(res.min) / 10.0f, static_cast<float>(res.max) / 10.0f};
        } else if constexpr (Param == MotorParameter::CurKpKi) {
            return {static_cast<float>(res.kp) / 10000.0f, static_cast<float>(res.ki) / 10.0f};
        } else if constexpr (Param == MotorParameter::SpdKpKi) {
            const float gain_scale = LoopGainScaleFor(impl_->ranges.fw_version);
            return {static_cast<float>(res.kp) / gain_scale,
                    static_cast<float>(res.ki) / gain_scale};
        } else if constexpr (Param == MotorParameter::PosKpKd) {
            const float gain_scale = LoopGainScaleFor(impl_->ranges.fw_version);
            return {static_cast<float>(res.kp) / gain_scale,
                    static_cast<float>(res.kd) / gain_scale};
        } else {
            return static_cast<RetType>(res);
        }
    }
}
// 显式实例化需带 ENCOS_BASE_API，MinGW/GCC 才会把成员函数模板导出到 DLL（见 motor_control.cc）。
template ENCOS_BASE_API float Motor::GetParameter<MotorParameter::Position>();
template ENCOS_BASE_API float Motor::GetParameter<MotorParameter::Speed>();
template ENCOS_BASE_API float Motor::GetParameter<MotorParameter::Current>();
template ENCOS_BASE_API float Motor::GetParameter<MotorParameter::Power>();
template ENCOS_BASE_API float Motor::GetParameter<MotorParameter::Kt>();
template ENCOS_BASE_API Range<uint16_t> Motor::GetParameter<MotorParameter::PVTKpRange>();
template ENCOS_BASE_API Range<uint16_t> Motor::GetParameter<MotorParameter::PVTKdRange>();
template ENCOS_BASE_API Range<float> Motor::GetParameter<MotorParameter::PVTPosRange>();
template ENCOS_BASE_API Range<float> Motor::GetParameter<MotorParameter::PVTSpdRange>();
template ENCOS_BASE_API Range<float> Motor::GetParameter<MotorParameter::PVTTorRange>();
template ENCOS_BASE_API Range<float> Motor::GetParameter<MotorParameter::PVTCurRange>();
template ENCOS_BASE_API int Motor::GetParameter<MotorParameter::UUID>();
template ENCOS_BASE_API uint16_t Motor::GetParameter<MotorParameter::CanTimeout>();
template ENCOS_BASE_API KpKi<float> Motor::GetParameter<MotorParameter::CurKpKi>();
template ENCOS_BASE_API KpKi<float> Motor::GetParameter<MotorParameter::SpdKpKi>();
template ENCOS_BASE_API KpKd<float> Motor::GetParameter<MotorParameter::PosKpKd>();
template ENCOS_BASE_API uint16_t Motor::GetParameter<MotorParameter::BrakeStatus>();
template ENCOS_BASE_API MotorEncoderAngle Motor::GetParameter<MotorParameter::EncoderAngle>();
template ENCOS_BASE_API uint32_t Motor::GetParameter<MotorParameter::ProductId>();
template ENCOS_BASE_API uint32_t Motor::GetParameter<MotorParameter::HardwareVersion>();
template ENCOS_BASE_API uint32_t Motor::GetParameter<MotorParameter::FirmwareVersion>();

}  // namespace encos
