#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "encos/export.h"
#include "motor/types.h"

namespace encos {

/**
 * @brief Fake 电机快照
 */
struct FakeMotorSnapshot {
    MotorModel model = MotorModel::EC_A4310_P2;  ///< 模拟电机型号
    MotorPVTRanges ranges{};                     ///< PVT 控制范围
    float position_rad = 0.0f;                   ///< 位置（rad）
    float speed_rad_s = 0.0f;                    ///< 速度（rad/s）
    float current_a = 0.0f;                      ///< 电流（A）
    float torque_nm = 0.0f;                      ///< 转矩（N·m）
    float motor_temp_c = 25.0f;                  ///< 电机温度（°C）
    float mos_temp_c = 25.0f;                    ///< MOS 温度（°C）
    float acceleration = 5.0f;                   ///< 加速度参数
    float kt = 0.0f;                             ///< 转矩常数（N·m/A）
    uint16_t can_timeout_ms = 1000;              ///< CAN 超时（ms）
    uint8_t reply_frame_flags = 0;               ///< 回复帧格式标志
    MotorCommunicationMode communication_mode = MotorCommunicationMode::ClassicCan;  ///< 通信模式
    MotorError error = MotorError::NoError;  ///< 模拟错误状态
    bool brake_enabled = false;              ///< 制动是否启用
};

/**
 * @brief Fake 适配器自动回复模式
 */
enum class FakeReplyMode { Automatic, Manual };

/**
 * @brief Fake 参数写入策略
 */
enum class FakeWritePolicy { Success, Failure, Ignore };

/**
 * @brief Fake 解码命令类型
 */
enum class FakeCommandKind {
    Unknown,
    PVTControl,
    PosControl,
    SpdControl,
    CurControl,
    TorControl,
    Stop,
    Brake,
    SetId,
    SetPos,
    ResetZeroPos,
    GetParameter,
    SetParameter,
};

/**
 * @brief Fake PVT 控制命令载荷
 */
struct FakePVTControlPayload {
    float kp = 0.0f;        ///< 比例增益
    float kd = 0.0f;        ///< 微分增益
    float position = 0.0f;  ///< 目标位置（rad）
    float speed = 0.0f;     ///< 目标速度（rad/s）
    float torque = 0.0f;    ///< 目标转矩（N·m）
};

/**
 * @brief Fake 位置控制命令载荷
 */
struct FakePosControlPayload {
    float position = 0.0f;  ///< 目标位置（rad）
    float speed = 0.0f;     ///< 最大速度（rad/s）
    float current = 0.0f;   ///< 最大电流（A）
    int feedback_type = 0;  ///< 反馈类型
};

/**
 * @brief Fake 速度控制命令载荷
 */
struct FakeSpdControlPayload {
    float speed = 0.0f;     ///< 目标速度（rad/s）
    float current = 0.0f;   ///< 最大电流（A）
    int feedback_type = 0;  ///< 反馈类型
};

/**
 * @brief Fake 电流控制命令载荷
 */
struct FakeCurControlPayload {
    float current = 0.0f;   ///< 目标电流（A）
    int feedback_type = 0;  ///< 反馈类型
};

/**
 * @brief Fake 扭矩控制命令载荷
 */
struct FakeTorControlPayload {
    float torque = 0.0f;    ///< 目标转矩（N·m）
    int feedback_type = 0;  ///< 反馈类型
};

/**
 * @brief Fake 停止命令载荷
 */
struct FakeStopPayload {
    MotorStopMode mode = MotorStopMode::FullBrake;  ///< 停止方式
    float current = 0.0f;                           ///< 制动电流（A）
    int feedback_type = 0;                          ///< 反馈类型
};

/**
 * @brief Fake 刹车命令载荷
 */
struct FakeBrakePayload {
    bool enabled = false;       ///< 是否启用制动
    bool wait_for_ack = false;  ///< 是否等待确认帧
};

/**
 * @brief Fake 设置 ID 命令载荷
 */
struct FakeSetIdPayload {
    int source_id = 0;          ///< 原电机 ID
    int target_id = 0;          ///< 新电机 ID
    bool wait_for_ack = false;  ///< 是否等待确认帧
};

/**
 * @brief Fake 设置位置命令载荷
 */
struct FakeSetPosPayload {
    float position_rad = 0.0f;  ///< 要写入的位置（rad）
    bool wait_for_ack = true;   ///< 是否等待确认帧
};

/**
 * @brief Fake 复位零位命令载荷
 */
struct FakeResetZeroPosPayload {
    bool wait_for_ack = true;      ///< 是否等待确认帧
    bool is_legacy_reset = false;  ///< 是否使用旧版复零命令
};

/**
 * @brief Fake 读取参数命令载荷
 */
struct FakeGetParameterPayload {
    MotorParameter parameter = MotorParameter::Position;  ///< 要读取的参数
};

/**
 * @brief Fake 写入参数命令载荷
 */
struct FakeSetParameterPayload {
    std::optional<MotorParameter> parameter;  ///< 已识别的参数，未知时为空
    uint8_t raw_parameter_id = 0;             ///< 原始参数 ID
    std::vector<uint8_t> raw_value;           ///< 原始参数值
    bool wait_for_ack = false;                ///< 是否等待确认帧
};

/**
 * @brief Fake 命令载荷变体
 */
using FakeCommandPayload =
    std::variant<std::monostate, FakePVTControlPayload, FakePosControlPayload,
                 FakeSpdControlPayload, FakeCurControlPayload, FakeTorControlPayload,
                 FakeStopPayload, FakeBrakePayload, FakeSetIdPayload, FakeSetPosPayload,
                 FakeResetZeroPosPayload, FakeGetParameterPayload, FakeSetParameterPayload>;

/**
 * @brief Fake 解码命令记录
 */
struct FakeCommandRecord {
    int bus_idx = 0;                                  ///< 总线索引
    int motor_idx = 0;                                ///< 电机索引
    uint8_t raw_frame_flags = 0;                      ///< 原始帧格式标志
    FakeCommandKind kind = FakeCommandKind::Unknown;  ///< 解码后的命令类型
    std::string formatted_text;                       ///< 便于阅读的命令文本
    FakeCommandPayload payload;                       ///< 解码后的命令载荷
};

/**
 * @brief 已安装的 Fake 适配器控制能力接口
 *
 * 通过 BaseAdapter::GetFakeAdapterControl() 查询，仅对 Fake 适配器返回非空对象。
 * 调用方必须保持创建该控制接口的 BaseAdapterPtr 存活。
 */
class ENCOS_BASE_API FakeAdapterControl {
public:
    /** @brief 解码到命令后调用的观察器 */
    using DecodedCommandObserver = std::function<void(const FakeCommandRecord&)>;

    virtual ~FakeAdapterControl();

    /**
     * @brief 启用自动创建未知电机
     */
    virtual void EnableAutoCreateMotor() = 0;

    /**
     * @brief 禁用自动创建未知电机
     */
    virtual void DisableAutoCreateMotor() = 0;

    /**
     * @brief 设置自动回复模式
     */
    virtual void SetReplyMode(FakeReplyMode mode) = 0;

    /**
     * @brief 启用或禁用本地命令记录
     *
     * 禁用后仍会通过解码命令观察器通知外部，但不再保留历史记录。
     */
    virtual void EnableCommandRecording(bool enabled = true) = 0;

    /**
     * @brief 启用或禁用位置模拟随机误差
     *
     * 默认启用。禁用后，Fake 运动积分只累加确定性位置增量。
     */
    virtual void EnablePositionError(bool enabled = true) = 0;

    /**
     * @brief 设置解码命令观察器
     */
    virtual void SetDecodedCommandObserver(DecodedCommandObserver observer) = 0;

    /**
     * @brief 清除解码命令观察器
     */
    virtual void ClearDecodedCommandObserver() = 0;

    /**
     * @brief 获取指定电机的快照
     *
     * 调用会先把模拟运动推进到当前时刻，因此返回的是实时状态。
     */
    virtual FakeMotorSnapshot GetMotorSnapshot(int bus_idx, int motor_idx) = 0;

protected:
    FakeAdapterControl() = default;
};

}  // namespace encos
