#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include "motor_model_generated.h"

namespace encos {

/** @brief 电机停止方式 */
enum class MotorStopMode {
    FullBrake = 2,         ///< 全制动
    DynamicBrake = 3,      ///< 动态制动
    RegenerativeBrake = 4  ///< 回馈制动
};

/** @brief 电机通信协议模式 */
enum class MotorCommunicationMode : uint8_t {
    ClassicCan = 0x00,  ///< 经典 CAN
    CanFd = 0x01,       ///< CAN FD
    CanOpen = 0x02      ///< CANopen
};

/** @brief 可读取或设置的电机参数标识 */
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
/** @brief 总线传输使用的紧凑 CAN 消息 */
struct MotorPackMsg {
    uint32_t id;             ///< CAN 标识符
    uint8_t frame_flags{0};  ///< 帧格式标志，见 `kCanFrameFlag*`
    uint8_t len;             ///< 有效载荷长度
    uint8_t data[8];         ///< 有效载荷字节
};
#pragma pack(pop)

/** @brief 判断两条紧凑 CAN 消息是否完全相同 */
inline bool operator==(const MotorPackMsg& lhs, const MotorPackMsg& rhs) {
    return lhs.id == rhs.id && lhs.frame_flags == rhs.frame_flags && lhs.len == rhs.len &&
           std::memcmp(lhs.data, rhs.data, sizeof(lhs.data)) == 0;
}

static_assert(sizeof(MotorPackMsg) == 14, "MotorPackMsg size changed");

constexpr uint8_t kCanFrameFlagEff = 0x01;     ///< 扩展帧标识符标志
constexpr uint8_t kCanFrameFlagFdBit1 = 0x02;  ///< CAN FD 标志位 1
constexpr uint8_t kCanFrameFlagFdBit2 = 0x04;  ///< CAN FD 标志位 2
constexpr uint8_t kCanFrameFlagRtr = 0x08;     ///< 远程请求帧标志
constexpr uint8_t kCanFrameFlagMask = 0x0F;    ///< 已定义帧标志掩码
constexpr uint8_t kCanFrameFlagFdMask =
    kCanFrameFlagFdBit1 | kCanFrameFlagFdBit2;  ///< CAN FD 组合标志掩码

/** @brief 丢弃未定义的 CAN 帧标志位 */
inline uint8_t SanitizeCanFrameFlags(uint8_t flags) {
    return static_cast<uint8_t>(flags & kCanFrameFlagMask);
}

/** @brief 判断帧标志是否指定扩展 CAN 标识符 */
inline bool CanFrameFlagsUseExtendedId(uint8_t flags) {
    return (SanitizeCanFrameFlags(flags) & kCanFrameFlagEff) != 0;
}

/** @brief 判断帧标志是否指定远程请求帧 */
inline bool CanFrameFlagsUseRtr(uint8_t flags) {
    return (SanitizeCanFrameFlags(flags) & kCanFrameFlagRtr) != 0;
}

/** @brief 判断帧标志是否指定 CAN FD */
inline bool CanFrameFlagsUseCanFd(uint8_t flags) {
    return (SanitizeCanFrameFlags(flags) & kCanFrameFlagFdMask) == kCanFrameFlagFdMask;
}

/** @brief 判断 CAN FD 的两个组合标志位是否一致 */
inline bool CanFrameFlagsHaveValidCanFdBits(uint8_t flags) {
    const uint8_t fd_bits =
        static_cast<uint8_t>(SanitizeCanFrameFlags(flags) & kCanFrameFlagFdMask);
    return fd_bits == 0 || fd_bits == kCanFrameFlagFdMask;
}

/** @brief 带总线索引的电机消息 */
struct MotorMessage {
    int bus_idx;        ///< 总线索引
    MotorPackMsg data;  ///< CAN 消息内容
};

/** @brief 电机消息列表 */
using MotorMessages = std::vector<MotorMessage>;

/** @brief 数值范围 */
template <typename T>
struct Range {
    T min;  ///< 最小值
    T max;  ///< 最大值
};

/** @brief PI 控制器增益 */
template <typename T>
struct KpKi {
    T kp;  ///< 比例增益
    T ki;  ///< 积分增益
};

/** @brief PD 控制器增益 */
template <typename T>
struct KpKd {
    T kp;  ///< 比例增益
    T kd;  ///< 微分增益
};

/** @brief 固件语义版本号 */
struct Version {
    uint8_t major;  ///< 主版本号
    uint8_t minor;  ///< 次版本号
    uint8_t patch;  ///< 修订号
};

/** @brief 指定电机型号的 PVT 控制范围与转矩常数 */
struct MotorPVTRanges {
    Range<float> kp;        ///< 比例增益范围
    Range<float> kd;        ///< 微分增益范围
    Range<float> position;  ///< 位置范围（rad）
    Range<float> speed;     ///< 速度范围（rad/s）
    Range<float> torque;    ///< 转矩范围（N·m）
    Range<float> current;   ///< 电流范围（A）
    float kt;               ///< 转矩常数（N·m/A）
};

/** @brief 电机反馈错误状态 */
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

/** @brief 反馈类型 1：完整运动与温度状态 */
struct MotorFeedbackMsg1 {
    MotorError error = MotorError::NoResponse;                          ///< 错误状态
    float position = std::numeric_limits<float>::quiet_NaN();           ///< 位置（rad）
    float speed = std::numeric_limits<float>::quiet_NaN();              ///< 速度（rad/s）
    float current = std::numeric_limits<float>::quiet_NaN();            ///< 电流（A）
    float motor_temperature = std::numeric_limits<float>::quiet_NaN();  ///< 电机温度（°C）
    float mos_temperature = std::numeric_limits<float>::quiet_NaN();    ///< MOS 温度（°C）
};

/** @brief 反馈类型 2：位置、电流与电机温度 */
struct MotorFeedbackMsg2 {
    MotorError error = MotorError::NoResponse;  ///< 错误状态
    float position;                             ///< 位置（rad）
    float current;                              ///< 电流（A）
    float motor_temperature;                    ///< 电机温度（°C）
};

/** @brief 反馈类型 3：速度、电流与电机温度 */
struct MotorFeedbackMsg3 {
    MotorError error = MotorError::NoResponse;  ///< 错误状态
    float speed;                                ///< 速度（rad/s）
    float current;                              ///< 电流（A）
    float motor_temperature;                    ///< 电机温度（°C）
};

/** @brief 默认的完整电机状态类型 */
using MotorStatus = MotorFeedbackMsg1;

/** @brief 根据反馈编号选择对应反馈结构 */
template <int FeedbackType>
using FeedbackStruct =
    std::conditional_t<FeedbackType == 1, MotorFeedbackMsg1,
                       std::conditional_t<FeedbackType == 2, MotorFeedbackMsg2, MotorFeedbackMsg3>>;

/** @cond INTERNAL */
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
/** @endcond */

}  // namespace encos
