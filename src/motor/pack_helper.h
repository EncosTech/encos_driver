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

#include "motor/math_utils.h"
#include "motor/types.h"
#include "platform/delay.h"
#include "utils/constants.h"

namespace encos {

using namespace std::chrono;

constexpr milliseconds kWaitTimeout{500};
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

template <int FeedbackType>
std::optional<FeedbackStruct<FeedbackType>> DecodeFeedback(const MotorPackMsg& pack,
                                                           float current_range) {
    static_assert(FeedbackType == 1 || FeedbackType == 2 || FeedbackType == 3,
                  "Invalid FeedbackType: must be 1, 2 or 3");
    if (!packet_has_payload(pack, 0, 8))
        return std::nullopt;
    uint8_t ack = static_cast<uint8_t>(pack.data[0] >> 5);
    if (ack != FeedbackType)
        return std::nullopt;

    auto error = static_cast<MotorError>(pack.data[0] & 0x1F);

    if constexpr (FeedbackType == 1) {
        int pos_int = static_cast<int>(pack.data[1] << 8 | pack.data[2]);
        int spd_int = static_cast<int>(pack.data[3] << 4 | ((pack.data[4] & 0xF0) >> 4));
        int cur_int = static_cast<int>(((pack.data[4] & 0x0F) << 8) | pack.data[5]);

        MotorFeedbackMsg1 fb{};
        fb.error = error;
        fb.position = UintToFloat(pos_int, -12.5f, 12.5f, 16);
        fb.speed = UintToFloat(spd_int, -18.0f, 18.0f, 12);
        fb.current = UintToFloat(cur_int, -current_range, current_range, 12);
        fb.motor_temperature = (static_cast<float>(pack.data[6]) - 50.0f) / 2.0f;
        fb.mos_temperature = (static_cast<float>(pack.data[7]) - 50.0f) / 2.0f;
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
        fb.motor_temperature = (static_cast<float>(pack.data[7]) - 50.0f) / 2.0f;
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
        fb.motor_temperature = (static_cast<float>(pack.data[7]) - 50.0f) / 2.0f;
        return fb;
    }
}

inline MotorFeedbackMsg1 AutoDecodeFeedback(const MotorPackMsg& pack, float current_range) {
    uint8_t ack = static_cast<uint8_t>(pack.data[0] >> 5);
    switch (ack) {
        case 1: {
            auto fb_opt = DecodeFeedback<1>(pack, current_range);
            if (fb_opt)
                return *fb_opt;
            break;
        }
        case 2: {
            auto fb_opt = DecodeFeedback<2>(pack, current_range);
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
            auto fb_opt = DecodeFeedback<3>(pack, current_range);
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
                     float current_range, milliseconds timeout = kWaitTimeout)
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
                auto fb = DecodeFeedback<FeedbackType>(pack, current_range);
                if (fb)
                    return fb;
            }
            platform::SleepFor(kPollInterval);
        }
        return std::nullopt;
    }
}

};  // namespace encos
