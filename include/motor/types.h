#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include "motor_model_generated.h"

namespace encos {

enum class MotorStopMode { FullBrake = 2, DynamicBrake = 3, RegenerativeBrake = 4 };

enum class MotorCommunicationMode : uint8_t { ClassicCan = 0x00, CanFd = 0x01, CanOpen = 0x02 };

enum class MotorParameter : uint8_t {
    Position = 1,
    Speed,
    Current,
    Power,
    Acceleration,
    FluxKpGain,
    FluxKiGain,
    FbKpGain,
    PdGain,
    Kt = 22,
    PVTKpRange,
    PVTKdRange,
    PVTPosRange,
    PVTSpdRange,
    PVTTorRange,
    PVTCurRange,
    UUID,
    Version,
    CanTimeout,
    CurKpKi,
    SpdKpKi,
    PosKpKd,
    BrakeStatus = 37
};

#pragma pack(push, 1)
struct MotorPackMsg {
    uint32_t id;
    uint8_t frame_flags{0};
    uint8_t len;
    uint8_t data[8];
};
#pragma pack(pop)

inline bool operator==(const MotorPackMsg& lhs, const MotorPackMsg& rhs) {
    return lhs.id == rhs.id && lhs.frame_flags == rhs.frame_flags && lhs.len == rhs.len &&
           std::memcmp(lhs.data, rhs.data, sizeof(lhs.data)) == 0;
}

static_assert(sizeof(MotorPackMsg) == 14, "MotorPackMsg size changed");

constexpr uint8_t kCanFrameFlagEff = 0x01;
constexpr uint8_t kCanFrameFlagFdBit1 = 0x02;
constexpr uint8_t kCanFrameFlagFdBit2 = 0x04;
constexpr uint8_t kCanFrameFlagRtr = 0x08;
constexpr uint8_t kCanFrameFlagMask = 0x0F;
constexpr uint8_t kCanFrameFlagFdMask = kCanFrameFlagFdBit1 | kCanFrameFlagFdBit2;

inline uint8_t SanitizeCanFrameFlags(uint8_t flags) {
    return static_cast<uint8_t>(flags & kCanFrameFlagMask);
}

inline bool CanFrameFlagsUseExtendedId(uint8_t flags) {
    return (SanitizeCanFrameFlags(flags) & kCanFrameFlagEff) != 0;
}

inline bool CanFrameFlagsUseRtr(uint8_t flags) {
    return (SanitizeCanFrameFlags(flags) & kCanFrameFlagRtr) != 0;
}

inline bool CanFrameFlagsUseCanFd(uint8_t flags) {
    return (SanitizeCanFrameFlags(flags) & kCanFrameFlagFdMask) == kCanFrameFlagFdMask;
}

inline bool CanFrameFlagsHaveValidCanFdBits(uint8_t flags) {
    const uint8_t fd_bits =
        static_cast<uint8_t>(SanitizeCanFrameFlags(flags) & kCanFrameFlagFdMask);
    return fd_bits == 0 || fd_bits == kCanFrameFlagFdMask;
}

struct MotorMessage {
    int bus_idx;
    MotorPackMsg data;
};

using MotorMessages = std::vector<MotorMessage>;

template <typename T>
struct Range {
    T min;
    T max;
};

template <typename T>
struct KpKi {
    T kp;
    T ki;
};

template <typename T>
struct KpKd {
    T kp;
    T kd;
};

struct Version {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
};

struct MotorPVTRanges {
    Range<float> kp;
    Range<float> kd;
    Range<float> position;
    Range<float> speed;
    Range<float> torque;
    Range<float> current;
    float kt;
};

enum class MotorError : uint8_t {
    NoError = 0,
    OverTemperature = 1,
    OverCurrent = 2,
    VoltageHigh = 3,
    VoltageLow = 4,
    EncoderError = 5,
    BrakeVoltageHigh = 6,
    DriverError = 7,
    OverTemperatureWarning = 8,
    NoResponse = 255
};

struct MotorFeedbackMsg1 {
    MotorError error = MotorError::NoResponse;
    float position = std::numeric_limits<float>::quiet_NaN();
    float speed = std::numeric_limits<float>::quiet_NaN();
    float current = std::numeric_limits<float>::quiet_NaN();
    float motor_temperature = std::numeric_limits<float>::quiet_NaN();
    float mos_temperature = std::numeric_limits<float>::quiet_NaN();
};

struct MotorFeedbackMsg2 {
    MotorError error = MotorError::NoResponse;
    float position;
    float current;
    float motor_temperature;
};

struct MotorFeedbackMsg3 {
    MotorError error = MotorError::NoResponse;
    float speed;
    float current;
    float motor_temperature;
};

using MotorStatus = MotorFeedbackMsg1;

template <int FeedbackType>
using FeedbackStruct =
    std::conditional_t<FeedbackType == 1, MotorFeedbackMsg1,
                       std::conditional_t<FeedbackType == 2, MotorFeedbackMsg2, MotorFeedbackMsg3>>;

template <MotorParameter T>
struct MotorParameterTraits {
    using RawType = void;
    using RetType = void;
};

template <>
struct MotorParameterTraits<MotorParameter::Position> {
    using RawType = float;
    using RetType = float;
};

template <>
struct MotorParameterTraits<MotorParameter::Speed> {
    using RawType = float;
    using RetType = float;
};

template <>
struct MotorParameterTraits<MotorParameter::Current> {
    using RawType = float;
    using RetType = float;
};

template <>
struct MotorParameterTraits<MotorParameter::Power> {
    using RawType = float;
    using RetType = float;
};

template <>
struct MotorParameterTraits<MotorParameter::Acceleration> {
    using RawType = float;
    using RetType = float;
};

template <>
struct MotorParameterTraits<MotorParameter::FluxKpGain> {
    using RawType = uint16_t;
    using RetType = uint16_t;
};

template <>
struct MotorParameterTraits<MotorParameter::FluxKiGain> {
    using RawType = uint16_t;
    using RetType = uint16_t;
};

template <>
struct MotorParameterTraits<MotorParameter::FbKpGain> {
    using RawType = uint16_t;
    using RetType = uint16_t;
};

template <>
struct MotorParameterTraits<MotorParameter::PdGain> {
    using RawType = uint16_t;
    using RetType = uint16_t;
};

template <>
struct MotorParameterTraits<MotorParameter::Kt> {
    using RawType = uint16_t;
    using RetType = float;
};

template <>
struct MotorParameterTraits<MotorParameter::PVTKpRange> {
    using RawType = Range<uint16_t>;
    using RetType = Range<uint16_t>;
};

template <>
struct MotorParameterTraits<MotorParameter::PVTKdRange> {
    using RawType = Range<uint16_t>;
    using RetType = Range<uint16_t>;
};

template <>
struct MotorParameterTraits<MotorParameter::PVTPosRange> {
    using RawType = Range<int16_t>;
    using RetType = Range<float>;
};

template <>
struct MotorParameterTraits<MotorParameter::PVTSpdRange> {
    using RawType = Range<int16_t>;
    using RetType = Range<float>;
};

template <>
struct MotorParameterTraits<MotorParameter::PVTTorRange> {
    using RawType = Range<int16_t>;
    using RetType = Range<float>;
};

template <>
struct MotorParameterTraits<MotorParameter::PVTCurRange> {
    using RawType = Range<int16_t>;
    using RetType = Range<float>;
};

template <>
struct MotorParameterTraits<MotorParameter::UUID> {
    using RawType = int;
    using RetType = int;
};

template <>
struct MotorParameterTraits<MotorParameter::Version> {
    using RawType = Version;
    using RetType = Version;
};

template <>
struct MotorParameterTraits<MotorParameter::CanTimeout> {
    using RawType = uint16_t;
    using RetType = uint16_t;
};

template <>
struct MotorParameterTraits<MotorParameter::CurKpKi> {
    using RawType = KpKi<uint16_t>;
    using RetType = KpKi<float>;
};

template <>
struct MotorParameterTraits<MotorParameter::SpdKpKi> {
    using RawType = KpKi<uint16_t>;
    using RetType = KpKi<float>;
};

template <>
struct MotorParameterTraits<MotorParameter::PosKpKd> {
    using RawType = KpKd<uint16_t>;
    using RetType = KpKd<float>;
};

template <>
struct MotorParameterTraits<MotorParameter::BrakeStatus> {
    using RawType = uint16_t;
    using RetType = uint16_t;
};

template <MotorParameter T>
using MotorParameterRawType = typename MotorParameterTraits<T>::RawType;

template <MotorParameter T>
using MotorParameterRetType = typename MotorParameterTraits<T>::RetType;

}  // namespace encos
