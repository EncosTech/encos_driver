// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

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

/** @brief 可读取或设置的电机参数标识
 *
 * 枚举值即查询命令的线上子命令号。新版协议删除的旧子命令（0x05、0x06~0x09、0x1E）
 * 直接移除并保留空洞，不复用其数值。
 */
enum class MotorParameter : uint8_t {
    Position = 1,
    Speed = 2,
    Current = 3,
    Power = 4,
    Kt = 22,
    PVTKpRange = 23,
    PVTKdRange = 24,
    PVTPosRange = 25,
    PVTSpdRange = 26,
    PVTTorRange = 27,
    PVTCurRange = 28,
    UUID = 29,
    CanTimeout = 31,
    CurKpKi = 32,
    SpdKpKi = 33,
    PosKpKd = 34,
    BrakeStatus = 37,
    EncoderAngle = 39,
    ProductId = 48,
    HardwareVersion = 49,
    FirmwareVersion = 50
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

/** @brief 输出编码器角度查询结果（0x27） */
struct MotorEncoderAngle {
    uint8_t cal_flag{0};   ///< 校准标志：0 未校准，1 已校准
    float raw_angle{0.f};  ///< 原始角度（°），归一化到 ±180
    float cal_angle{0.f};  ///< 校准角度（°），归一化到 ±180
};

/** @brief 指定电机型号的控制属性、范围与能力 */
struct MotorProperties {
    Range<float> kp;        ///< 比例增益范围
    Range<float> kd;        ///< 微分增益范围
    Range<float> position;  ///< 位置范围（rad）
    Range<float> speed;     ///< 速度范围（rad/s）
    Range<float> torque;    ///< 转矩范围（N·m）
    Range<float> current;   ///< 电流范围（A）
    float kt;               ///< 转矩常数（N·m/A）
    bool brake{false};      ///< 是否带机械抱闸（仅型号初始化时由型号表给出）
    uint32_t fw_version{0};  ///< 固件版本（0x32 查询结果，BCD `0xYYMMDDHH`）；0 表示旧版固件或未知
};

/** @deprecated 已更名为 `MotorProperties` */
using MotorPVTRanges [[deprecated("Use MotorProperties instead")]] = MotorProperties;

/** @brief 电机反馈错误状态
 *
 * 现有成员的值不得改动（外部代码按值比较）。旧版固件与新版固件的故障码映射见
 * `DecodeMotorError`：`6`/`7`/`8` 在两版语义不同，`OutputEncoderError` 及之后的成员
 * 只可能由新版固件产生。
 */
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
    OutputEncoderError = 9,
    FetOverTemperature = 10,
    OverSpeed = 11,
    TemperatureWarning = 12,
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

/** @brief 反馈类型 2：位置、电流与温度 */
struct MotorFeedbackMsg2 {
    MotorError error = MotorError::NoResponse;  ///< 错误状态
    float position;                             ///< 位置（rad）
    float current;                              ///< 电流（A）
    float motor_temperature;  ///< 温度（°C）：旧版固件为电机温度，新版固件为电机与 FET 中较高者
};

/** @brief 反馈类型 3：速度、电流与温度 */
struct MotorFeedbackMsg3 {
    MotorError error = MotorError::NoResponse;  ///< 错误状态
    float speed;                                ///< 速度（rad/s）
    float current;                              ///< 电流（A）
    float motor_temperature;  ///< 温度（°C）：旧版固件为电机温度，新版固件为电机与 FET 中较高者
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
    using RawType = uint8_t;
    using RetType = uint16_t;
};

template <>
struct MotorParameterTraits<MotorParameter::EncoderAngle> {
    using RawType = MotorEncoderAngle;
    using RetType = MotorEncoderAngle;
};

template <>
struct MotorParameterTraits<MotorParameter::ProductId> {
    using RawType = uint32_t;
    using RetType = uint32_t;
};

template <>
struct MotorParameterTraits<MotorParameter::HardwareVersion> {
    using RawType = uint32_t;
    using RetType = uint32_t;
};

template <>
struct MotorParameterTraits<MotorParameter::FirmwareVersion> {
    using RawType = uint32_t;
    using RetType = uint32_t;
};

template <MotorParameter T>
using MotorParameterRawType = typename MotorParameterTraits<T>::RawType;

template <MotorParameter T>
using MotorParameterRetType = typename MotorParameterTraits<T>::RetType;
/** @endcond */

}  // namespace encos
