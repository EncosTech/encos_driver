// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

#include "device/motor/math_utils.h"
#include "device/motor/types.h"
#include "platform/delay.h"
#include "utils/constants.h"

namespace encos {

using namespace std::chrono;

constexpr milliseconds kWaitTimeout{500};
/**
 * @brief 初始化时探测固件版本（0x32）的等待时长
 *
 * 旧版固件不响应该命令，因此探测使用远短于普通命令的超时，避免每条初始化路径
 * （尤其是逐台扫描的 `Bus::ScanMotors()`）为每台电机多等一个完整超时。
 */
constexpr milliseconds kFirmwareProbeTimeout{100};
#ifdef __EMSCRIPTEN__
constexpr milliseconds kPollInterval{2};
#else
constexpr milliseconds kPollInterval{1};
#endif

static_assert(sizeof(float) == sizeof(std::uint32_t), "Motor protocol requires 32-bit float");

inline std::uint32_t FloatBits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline bool packet_has_payload(const MotorPackMsg& pack, std::size_t offset, std::size_t size) {
    return pack.len >= offset + size;
}

inline uint16_t read_u16_be(const uint8_t* data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                                 static_cast<uint16_t>(data[1]));
}

inline int16_t read_i16_be(const uint8_t* data) {
    return static_cast<int16_t>(read_u16_be(data));
}

inline uint32_t read_u32_be(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

inline int32_t read_i32_be(const uint8_t* data) {
    return static_cast<int32_t>(read_u32_be(data));
}

inline float read_float_be(const uint8_t* data) {
    const uint32_t raw = read_u32_be(data);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

/**
 * @brief 按固件版本解码温度反馈
 * @param raw 报文字节的原始值
 * @param fw_version 电机固件版本，0 表示旧版固件
 * @return 温度（°C）
 *
 * 旧版固件 `T = (raw - 50) / 2`（分辨率 0.5°C），新版固件 `T = raw - 55`（分辨率 1°C）。
 */
inline float DecodeTemperature(uint8_t raw, uint32_t fw_version) {
    return fw_version == 0 ? (static_cast<float>(raw) - 50.0f) / 2.0f
                           : static_cast<float>(raw) - 55.0f;
}

/**
 * @brief 按固件版本解码故障码
 * @param raw 报文 `D[0]` 的低 5 位
 * @param fw_version 电机固件版本，0 表示旧版固件
 * @return 对应的 `MotorError`
 *
 * 两版的 `1~5` 语义一致；`6`/`7`/`8` 不同：旧版为刹车电压过高、DRV 驱动错误、
 * 温度警告，新版为输出编码器故障、FET 过温、超速，新版另有 `9` DRV 驱动故障与
 * `31` 温度警告。未列出的编码原样返回，保留信息。
 */
inline MotorError DecodeMotorError(uint8_t raw, uint32_t fw_version) {
    if (fw_version == 0) {
        return static_cast<MotorError>(raw);
    }
    switch (raw) {
        case 6:
            return MotorError::OutputEncoderError;
        case 7:
            return MotorError::FetOverTemperature;
        case 8:
            return MotorError::OverSpeed;
        case 9:
            return MotorError::DriverError;
        case 31:
            return MotorError::TemperatureWarning;
        default:
            return static_cast<MotorError>(raw);
    }
}

template <int FeedbackType>
std::optional<FeedbackStruct<FeedbackType>> DecodeFeedback(const MotorPackMsg& pack,
                                                           const MotorProperties& ranges) {
    static_assert(FeedbackType == 1 || FeedbackType == 2 || FeedbackType == 3,
                  "Invalid FeedbackType: must be 1, 2 or 3");
    if (!packet_has_payload(pack, 0, 8))
        return std::nullopt;
    uint8_t ack = static_cast<uint8_t>(pack.data[0] >> 5);
    if (ack != FeedbackType)
        return std::nullopt;

    auto error = DecodeMotorError(static_cast<uint8_t>(pack.data[0] & 0x1F), ranges.fw_version);

    if constexpr (FeedbackType == 1) {
        int pos_int = static_cast<int>(pack.data[1] << 8 | pack.data[2]);
        int spd_int = static_cast<int>(pack.data[3] << 4 | ((pack.data[4] & 0xF0) >> 4));
        int cur_int = static_cast<int>(((pack.data[4] & 0x0F) << 8) | pack.data[5]);

        MotorFeedbackMsg1 fb{};
        fb.error = error;
        fb.position = UintToFloat(pos_int, ranges.position.min, ranges.position.max, 16);
        fb.speed = UintToFloat(spd_int, ranges.speed.min, ranges.speed.max, 12);
        fb.current = UintToFloat(cur_int, ranges.current.min, ranges.current.max, 12);
        fb.motor_temperature = DecodeTemperature(pack.data[6], ranges.fw_version);
        fb.mos_temperature = DecodeTemperature(pack.data[7], ranges.fw_version);
        return fb;
    } else if constexpr (FeedbackType == 2) {
        uint32_t raw = (static_cast<uint32_t>(pack.data[1]) << 24) |
                       (static_cast<uint32_t>(pack.data[2]) << 16) |
                       (static_cast<uint32_t>(pack.data[3]) << 8) |
                       static_cast<uint32_t>(pack.data[4]);
        float position = 0.0f;
        std::memcpy(&position, &raw, sizeof(float));
        int cur_int = static_cast<int>(pack.data[5] << 8 | pack.data[6]);

        MotorFeedbackMsg2 fb{};
        fb.error = error;
        fb.position = position / 180.f * M_PIf;
        fb.current = static_cast<float>(cur_int) / 100.0f;
        fb.motor_temperature = DecodeTemperature(pack.data[7], ranges.fw_version);
        return fb;
    } else {
        uint32_t raw = (static_cast<uint32_t>(pack.data[1]) << 24) |
                       (static_cast<uint32_t>(pack.data[2]) << 16) |
                       (static_cast<uint32_t>(pack.data[3]) << 8) |
                       static_cast<uint32_t>(pack.data[4]);
        float speed = 0.0f;
        std::memcpy(&speed, &raw, sizeof(float));
        int cur_int = static_cast<int>(pack.data[5] << 8 | pack.data[6]);

        MotorFeedbackMsg3 fb{};
        fb.error = error;
        fb.speed = speed / 30.f * M_PIf;
        fb.current = static_cast<float>(cur_int) / 100.0f;
        fb.motor_temperature = DecodeTemperature(pack.data[7], ranges.fw_version);
        return fb;
    }
}

inline MotorFeedbackMsg1 AutoDecodeFeedback(const MotorPackMsg& pack,
                                            const MotorProperties& ranges) {
    uint8_t ack = static_cast<uint8_t>(pack.data[0] >> 5);
    switch (ack) {
        case 1: {
            auto fb_opt = DecodeFeedback<1>(pack, ranges);
            if (fb_opt)
                return *fb_opt;
            break;
        }
        case 2: {
            auto fb_opt = DecodeFeedback<2>(pack, ranges);
            if (fb_opt) {
                MotorFeedbackMsg2 fb2 = *fb_opt;
                MotorFeedbackMsg1 fb1{};
                fb1.error = fb2.error;
                fb1.position = fb2.position;
                fb1.speed = std::numeric_limits<float>::quiet_NaN();
                fb1.current = fb2.current;
                fb1.motor_temperature = fb2.motor_temperature;
                fb1.mos_temperature = std::numeric_limits<float>::quiet_NaN();
                return fb1;
            }
            break;
        }
        case 3: {
            auto fb_opt = DecodeFeedback<3>(pack, ranges);
            if (fb_opt) {
                MotorFeedbackMsg3 fb3 = *fb_opt;
                MotorFeedbackMsg1 fb1{};
                fb1.error = fb3.error;
                fb1.position = std::numeric_limits<float>::quiet_NaN();
                fb1.speed = fb3.speed;
                fb1.current = fb3.current;
                fb1.motor_temperature = fb3.motor_temperature;
                fb1.mos_temperature = std::numeric_limits<float>::quiet_NaN();
                return fb1;
            }
            break;
        }
        default:
            break;
    }
    MotorFeedbackMsg1 empty_fb{};
    empty_fb.error = MotorError::NoResponse;
    return empty_fb;
}

inline std::optional<MotorPackMsg> WaitForMatchingPacket(
    const std::function<MotorMessages()>& Read,
    const std::function<bool(const MotorPackMsg&)>& predicate,
    milliseconds timeout = kWaitTimeout) {
    const auto deadline = steady_clock::now() + timeout;
    while (steady_clock::now() < deadline) {
        for (const auto& message : Read()) {
            const auto& pack = message.data;
            if (predicate(pack)) {
                return pack;
            }
        }
        platform::SleepFor(kPollInterval);
    }

    return std::nullopt;
}

inline std::optional<bool> WaitForPacket(const std::function<MotorMessages()>& Read,
                                         const std::function<bool(const MotorPackMsg&)>& predicate,
                                         milliseconds timeout = kWaitTimeout) {
    return WaitForMatchingPacket(Read, predicate, timeout).has_value() ? std::optional<bool>{true}
                                                                       : std::nullopt;
}

template <int FeedbackType>
auto WaitForFeedback(const std::function<MotorMessages()>& Read, uint16_t motor_idx,
                     const MotorProperties& ranges, milliseconds timeout = kWaitTimeout)
    -> std::conditional_t<FeedbackType == 0, bool, std::optional<FeedbackStruct<FeedbackType>>> {
    static_assert(FeedbackType == 0 || FeedbackType == 1 || FeedbackType == 2 || FeedbackType == 3,
                  "Invalid FeedbackType: must be 0, 1, 2 or 3");

    if constexpr (FeedbackType == 0) {
        return std::nullopt;
    } else {
        const auto deadline = steady_clock::now() + timeout;
        while (steady_clock::now() < deadline) {
            for (const auto& message : Read()) {
                const auto& pack = message.data;
                if (pack.id != motor_idx)
                    continue;
                auto fb = DecodeFeedback<FeedbackType>(pack, ranges);
                if (fb)
                    return fb;
            }
            platform::SleepFor(kPollInterval);
        }
        return std::nullopt;
    }
}

};  // namespace encos
