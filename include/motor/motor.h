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

#include "encos/export.h"
#include "motor/types.h"
#include "platform/log.h"

namespace encos {

class Bus;
class EncosDriverManager;
class MotorWaiterTestAccess;
namespace wasm {
class RuntimeStore;
}

/**
 * @brief 电机接口类，表示总线上的单个电机
 *
 * 此类提供配置和控制电机的方法，以及获取参数/反馈。
 * 通过内部互斥锁保证线程安全。
 */
class ENCOS_BASE_API Motor {
    friend class EncosDriverManager;
    friend class Bus;
    friend class MotorWaiterTestAccess;
    friend class wasm::RuntimeStore;

private:
    explicit Motor(Bus* bus, uint16_t motor_idx, MotorModel model, LoggerPtr logger,
                   std::function<void(const MotorPackMsg&)> writer, uint8_t frame_flags = 0);
    explicit Motor(Bus* bus, uint16_t motor_idx, MotorModel model, LoggerPtr logger,
                   std::function<void(const MotorPackMsg&)> writer, uint8_t frame_flags,
                   bool canfd);
    explicit Motor(Bus* bus, uint16_t motor_idx, MotorPVTRanges ranges, LoggerPtr logger,
                   std::function<void(const MotorPackMsg&)> writer, uint8_t frame_flags = 0);
    explicit Motor(Bus* bus, uint16_t motor_idx, MotorPVTRanges ranges, LoggerPtr logger,
                   std::function<void(const MotorPackMsg&)> writer, uint8_t frame_flags,
                   bool canfd);
    explicit Motor(Bus* bus, uint16_t motor_idx, LoggerPtr logger,
                   std::function<void(const MotorPackMsg&)> writer, uint8_t frame_flags = 0);
    explicit Motor(Bus* bus, uint16_t motor_idx, LoggerPtr logger,
                   std::function<void(const MotorPackMsg&)> writer, uint8_t frame_flags,
                   bool canfd);

    /**
     * @brief 在调用方持有电机事务锁时发送 CAN 报文，并附带当前配置的帧标志
     * @param msg 要发送的电机消息
     */
    void SendMessageLocked(const MotorPackMsg& msg);

public:
    ~Motor();

    /**
     * @brief 启用状态与控制命令文件日志
     * @param base_name 不含状态/命令标识和文件后缀的基础名称
     */
    void EnableLog(const std::string& base_name);

    /**
     * @brief 刷新并关闭状态与控制命令文件日志
     */
    void DisableLog();

    /**
     * @brief 查询当前是否启用了完整的状态与控制命令日志
     * @return 两个日志写入器均存在时返回 true
     */
    bool IsLogged() const;

    /**
     * @brief 获取此电机包装器当前使用的 PVT 范围
     * @return 当前的 MotorPVTRanges 结构
     */
    MotorPVTRanges GetPVTRanges() const;

    /**
     * @brief 从电机固件初始化 PVT 参数
     *
     * 查询电机配置的 PVT 范围，并刷新当前包装器使用的范围与电流量程。
     */
    void InitMotorPVTParam();

    /**
     * @brief 设置此电机包装器使用的 PVT 范围
     * @param ranges 要应用的新 PVT 范围
     *
     * 此函数仅更新用于 PVT 控制输入的上层范围。
     * 不会向电机本身发送任何命令。
     */
    void SetDriverPVTRanges(const MotorPVTRanges& ranges);

    /**
     * @brief 设置电流反馈满量程范围
     * @param range 新的电流范围（单位：安培）
     *
     * 此函数仅更新用于电流反馈缩放的上层范围。
     * 不会向电机本身发送任何命令。
     */
    void SetCurrentRange(float range);

    /**
     * @brief 后续发送启用 CAN FD 帧标志
     */
    void EnableCanFd();

    /**
     * @brief 后续发送禁用 CAN FD 帧标志
     */
    void DisableCanFd();

    /**
     * @brief 查询后续发送是否启用 CAN FD 帧标志
     */
    bool IsCanFdEnabled() const;

    /**
     * @brief 后续发送启用 CAN 扩展帧标志
     */
    void EnableCanEff();

    /**
     * @brief 后续发送禁用 CAN 扩展帧标志
     */
    void DisableCanEff();

    /**
     * @brief 查询后续发送是否启用 CAN 扩展帧标志
     */
    bool IsCanEffEnabled() const;

    /** @name 电机设置
     * 用于配置电机地址和零位置的函数
     */
    /** @{ */

    /**
     * @brief 将电机 ID 更改为新索引
     * @param new_idx 要分配的新电机索引
     * @param wait_for_ack 如果为 true，等待确认包
     * @return 成功返回 true
     */
    bool SetId(uint16_t new_idx, bool wait_for_ack = true);

    /**
     * @brief 设置电机当前位置读数
     * @param now_pos 当前物理位置应写入的坐标值（rad）
     * @return 收到确认包返回 true，超时返回 false
     *
     * 此命令仅支持 CAN FD 能力电机；报文可通过当前配置的 CAN/CAN FD 帧标志发送。
     */
    bool SetPos(double now_pos);

    /**
     * @brief 重置电机的零位（参考）位置
     * @param wait_for_ack 如果为 true，等待确认包
     * @return 成功返回 true
     */
    bool ResetZeroPos(bool wait_for_ack = true);

    /**
     * @brief 获取电机状态
     * @param life_cycle_deduction 本次读取扣减的生命周期值，<=0 时不扣减
     * @return 电机状态，如果不可用则返回空
     */
    std::optional<MotorStatus> GetStatus(int life_cycle_deduction = 1);

    /**
     * @brief 注册电机状态回调
     *
     * 每当适配器接收到此电机的新状态数据时，回调将被调用。
     * 回调在传输接收线程中同步执行，不应执行耗时或阻塞工作。
     * 传入 nullptr 可取消注册。
     * @param callback 回调函数，参数为最新的 MotorStatus
     */
    void SetOnStatus(std::function<void(const MotorStatus&)> callback);

    /**
     * @brief 设置状态缓存最大生命周期
     * @param max_life_cycle 最大生命周期，INT_MAX 表示禁用生命周期清理
     */
    void SetMaxStatusLifeCycle(int max_life_cycle);

    /**
     * @brief 设置状态中值滤波窗口长度
     * @param window_size 窗口长度；0 或 1 时关闭中值滤波，至少为 2
     * 时启用；每次设置都会清空已有中值窗口
     */
    void SetStatusMedianFilterWindowSize(std::size_t window_size);

    /**
     * @brief 设置状态限幅滤波的各连续量最大帧间变化
     * @param position 位置最大变化；负值、NaN 或无穷大时关闭该量的限幅
     * @param speed 速度最大变化；负值、NaN 或无穷大时关闭该量的限幅
     * @param current 电流最大变化；负值、NaN 或无穷大时关闭该量的限幅
     * @param motor_temperature 电机温度最大变化；负值、NaN 或无穷大时关闭该量的限幅
     * @param mos_temperature MOS 温度最大变化；负值、NaN 或无穷大时关闭该量的限幅
     */
    void SetStatusLimitFilterMaxDeltas(float position, float speed, float current,
                                       float motor_temperature, float mos_temperature);

    /**
     * @brief 设置状态限幅滤波的各连续量最大帧间变化
     * @param limit_max_deltas 各连续量允许的最大帧间变化量结构
     */
    void SetStatusLimitFilterMaxDeltas(const MotorStatus& limit_max_deltas);

    /** @} */

    /** @name 电机控制
     * 电机控制函数，可选择返回反馈
     */
    /** @{ */

    /**
     * @brief 力位混控控制命令
     *
     * 发送前将输入限制在配置的范围内。
     * @tparam FeedbackType 如果为 0，指令发送后立即返回void，但PVT控制在任何情况下都会请求反馈；
     *                      如果为 1，指令发送后阻塞等待返回包含反馈的 MotorFeedbackMsg1
     * @param kp 比例增益（无量纲）
     * @param kd 微分增益（无量纲）
     * @param pos 目标位置（rad）
     * @param spd 目标速度（rad/s）
     * @param torque 目标扭矩（Nm）
     * @return 电机填充的反馈结构
     *
     * 返回的反馈字段在适用时使用 SI 单位：位置为弧度，
     * 速度为 rad/s，电流为 A，温度为摄氏度。
     */
    template <int FeedbackType>
    auto PVTControl(float kp, float kd, float pos, float spd,
                    float torque) -> std::conditional_t<FeedbackType == 0, void, MotorFeedbackMsg1>;

    /**
     * @brief 位置控制命令
     * @tparam FeedbackType 指令发送后立即返回void，是否请求反馈及反馈类型由feedback参数决定；
     *                      如果为 1/2/3，指令发送后阻塞等待返回包含不同反馈字段的
     * FeedbackStruct<FeedbackType>
     * @param position 目标位置（rad）
     * @param speed 最大速度（rad/s）
     * @param current 最大电流（A）
     * @return 参见 @tparam 描述
     */
    template <int FeedbackType>
    auto PosControl(float position, float speed, float current, int feedback = FeedbackType)
        -> std::conditional_t<FeedbackType == 0, void, FeedbackStruct<FeedbackType>>;

    /**
     * @brief 速度控制命令
     * @tparam FeedbackType 反馈行为参见 `PosControl`
     * @param speed 目标速度（rad/s）
     * @param current 最大电流（A）
     * @return 参见 `PosControl`
     */
    template <int FeedbackType>
    auto SpdControl(float speed, float current, int feedback = FeedbackType)
        -> std::conditional_t<FeedbackType == 0, void, FeedbackStruct<FeedbackType>>;

    /**
     * @brief 电流控制命令
     * @tparam FeedbackType 反馈行为参见 `PosControl`
     * @param current 目标电流（A）
     * @return 参见 `PosControl`
     */
    template <int FeedbackType>
    auto CurControl(float current, int feedback = FeedbackType)
        -> std::conditional_t<FeedbackType == 0, void, FeedbackStruct<FeedbackType>>;

    /**
     * @brief 扭矩控制命令
     * @tparam FeedbackType 反馈行为参见 `PosControl`
     * @param torque 目标扭矩（Nm）
     * @return 参见 `PosControl`
     */
    template <int FeedbackType>
    auto TorControl(float torque, int feedback = FeedbackType)
        -> std::conditional_t<FeedbackType == 0, void, FeedbackStruct<FeedbackType>>;

    /**
     * @brief 使用选定的模式使电机刹车
     * @tparam FeedbackType 反馈行为参见 `PosControl`
     * @param mode 停止模式（参见 MotorStopMode 枚举）。详细行为：
     *             - `MotorStopMode::FullBrake` 变阻尼控制模式，电流值自动忽略
     *             - `MotorStopMode::DynamicBrake` 能耗制动模式，此时电流为刹车过程最大电流
     *             - `MotorStopMode::RegenerativeBrake` 再生制动模式，此时电流为刹车过程最大电流
     * @param current 停止/制动期间使用的最大电流（A）
     * @return 参见 `PosControl`
     */
    template <int FeedbackType>
    auto Stop(MotorStopMode mode, float current, int feedback = FeedbackType)
        -> std::conditional_t<FeedbackType == 0, void, FeedbackStruct<FeedbackType>>;

    /**
     * @brief 启用或禁用刹车抱闸
     * @param enabled true 启用制动，false 释放
     * @param wait_for_ack 如果为 true，等待确认包
     * @return 如果请求的状态已应用则返回 true
     */
    bool Brake(bool enabled, bool wait_for_ack = true);

    /**
     * @brief 驱动电机运动至限位
     * @param limit 限位范围（最小..最大），单位弧度
     * @param dir 移动方向（1 或 -1）
     * @param spd 移动速度（rad/s）
     * @param cur 最大电流（A）
     * @param timeout 超时时间（毫秒）
     * @return 零点相对当前零点的偏置值（rad），失败返回 NaN
     */
    float GotoLimit(Range<float> limit, int dir, float spd = 2.f, float cur = 2.f,
                    std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});

    /**
     * @brief 驱动电机运动至零点并重置零位
     * @param offset 零点相对当前零点的偏置值（rad），通常由 GotoLimit 返回
     * @param spd 移动速度（rad/s）
     * @param cur 最大电流（A）
     * @param timeout 超时时间（毫秒）
     * @return 成功返回 true
     */
    bool GotoZero(float offset, float spd = 2.f, float cur = 2.f,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});

    /**
     * @brief 在指定限制范围内校准电机（依次调用 GotoLimit 和 GotoZero）
     * @param limit 校准范围（最小..最大），单位弧度
     * @param dir 校准期间移动的方向（1 或 -1）
     * @param spd 校准期间使用的速度（rad/s）
     * @param cur 校准期间使用的电流（A）
     * @param timeout 每个阶段的超时时间（毫秒）
     * @return 校准成功返回 true
     */
    bool Calibrate(Range<float> limit, int dir, float spd = 2.f, float cur = 2.f,
                   std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});

    /** @} */

    /** @name 控制参数
     * 用于配置控制增益、范围和超时的函数
     */
    /** @{ */

    /**
     * @brief 设置电机加速度
     * @param acceleration 要设置的值（单位：rad/s^2 或固件单位）
     * @param wait_for_ack 如果为 true，等待确认
     * @return 成功返回 true
     */
    bool SetAcceleration(float acceleration, bool wait_for_ack = true);

    /**
     * @brief 设置电机扭矩常数 `Kt`
     * @param kt 扭矩常数值（单位：Nm/A）
     * @param wait_for_ack 如果为 true，等待确认
     * @return 成功返回 true
     */
    bool SetKt(float kt, bool wait_for_ack = true);

    /**
     * @brief 设置 PVT Kp 范围
     * @param kp_range Kp 范围
     * @param wait_for_ack 如果为 true，等待确认
     * @return 成功返回 true
     */
    bool SetPVTKpRange(Range<uint16_t> kp_range, bool wait_for_ack = true);

    /**
     * @brief 设置 PVT Kd 范围
     * @param kd_range Kd 范围
     * @param wait_for_ack 如果为 true，等待确认
     * @return 成功返回 true
     */
    bool SetPVTKdRange(Range<uint16_t> kd_range, bool wait_for_ack = true);

    /**
     * @brief 设置 PVT 位置范围（浮点，根据系统为米/弧度）
     * @param pos_range 位置范围
     * @param wait_for_ack 如果为 true，等待确认
     * @return 成功返回 true
     */
    bool SetPVTPosRange(Range<float> pos_range, bool wait_for_ack = true);

    /**
     * @brief 设置 PVT 速度范围
     * @param spd_range 速度范围（单位：rad/s）
     * @param wait_for_ack 如果为 true，等待确认
     * @return 成功返回 true
     */
    bool SetPVTSpdRange(Range<float> spd_range, bool wait_for_ack = true);

    /**
     * @brief 设置 PVT 扭矩范围
     * @param tor_range 扭矩范围（单位：Nm）
     * @param wait_for_ack 如果为 true，等待确认
     * @return 成功返回 true
     */
    bool SetPVTTorRange(Range<float> tor_range, bool wait_for_ack = true);

    /**
     * @brief 设置 PVT 电流范围
     * @param cur_range 电流范围（单位：A）
     * @param wait_for_ack 如果为 true，等待确认
     * @return 成功返回 true
     * @warning 如果使用自动反馈模式，反馈范围在适配器重新初始化之前不会更新
     */
    bool SetPVTCurRange(Range<float> cur_range, bool wait_for_ack = true);

    /**
     * @brief 设置电流环 PI 增益
     * @param kp 比例增益
     * @param ki 积分增益
     * @param wait_for_ack 如果为 true，等待确认
     * @return 成功返回 true
     */
    bool SetCurPI(float kp, float ki, bool wait_for_ack = true);

    /**
     * @brief 设置速度环 PI 增益
     * @param kp 比例增益
     * @param ki 积分增益
     * @param wait_for_ack 如果为 true，等待确认
     * @return 成功返回 true
     */
    bool SetSpdPI(float kp, float ki, bool wait_for_ack = true);

    /**
     * @brief 设置位置环 PD 增益
     * @param kp 比例增益
     * @param kd 微分增益
     * @param wait_for_ack 如果为 true，等待确认
     * @return 成功返回 true
     */
    bool SetPosPD(float kp, float kd, bool wait_for_ack = true);

    /**
     * @brief 设置电机使用的 CAN 超时时间（毫秒）
     * @param timeout_ms 超时时间（ms）
     * @param wait_for_ack 如果为 true，等待确认
     * @return 成功返回 true
     */
    bool SetCanTimeout(uint16_t timeout_ms, bool wait_for_ack = true);

    /**
     * @brief 设置电机重启后使用的通信模式
     * @param mode 通信模式配置值
     * @param wait_for_ack 如果为 true，等待确认
     * @return 成功返回 true
     *
     * 此配置按固件协议重启后生效，不修改当前 `Motor` 对象的本地帧标志。
     */
    bool SetCommunicationMode(MotorCommunicationMode mode, bool wait_for_ack = true);

    /**
     * @brief 从电机获取参数
     * @tparam Param 要查询的参数（参见 MotorParameter 枚举）
     * @return 类型化的返回值，由 `MotorParameterTraits` 和 `MotorParameterRetType` 定义
     *
     * 支持的参数及其返回类型/单位：
     * - `MotorParameter::Position`     -> `float` (rad)
     * - `MotorParameter::Speed`        -> `float` (rad/s)
     * - `MotorParameter::Current`      -> `float` (A)
     * - `MotorParameter::Power`        -> `float` (W)
     * - `MotorParameter::Acceleration` -> `float` (rad/s^2)
     * - `MotorParameter::FluxKpGain`   -> `uint16_t` (原始增益单位)
     * - `MotorParameter::FluxKiGain`   -> `uint16_t` (原始增益单位)
     * - `MotorParameter::FbKpGain`     -> `uint16_t` (原始增益单位)
     * - `MotorParameter::PdGain`       -> `uint16_t` (原始增益单位)
     * - `MotorParameter::Kt`           -> `float` (Nm/A)
     * - `MotorParameter::PVTKpRange`   -> `Range<uint16_t>` (原始范围表示)
     * - `MotorParameter::PVTKdRange`   -> `Range<uint16_t>` (原始范围表示)
     * - `MotorParameter::PVTPosRange`  -> `Range<float>` (rad)
     * - `MotorParameter::PVTSpdRange`  -> `Range<float>` (rad/s)
     * - `MotorParameter::PVTTorRange`  -> `Range<float>` (Nm)
     * - `MotorParameter::PVTCurRange`  -> `Range<float>` (A)
     * - `MotorParameter::UUID`         -> `int`
     * - `MotorParameter::Version`      -> `Version` (结构体: major/minor/patch)
     * - `MotorParameter::CanTimeout`   -> `uint16_t` (ms)
     * - `MotorParameter::CurKpKi`      -> `KpKi` (kp, ki 浮点数)
     * - `MotorParameter::SpdKpKi`      -> `KpKi` (kp, ki 浮点数)
     * - `MotorParameter::PosKpKd`      -> `KpKd` (kp, kd 浮点数)
     * - `MotorParameter::BrakeStatus`  -> `uint16_t` (状态位)
     *
     * 注意事项：
     * - 模板返回上面列出的类型；调用者应相应选择 `Param`
     * - 此函数会在等待电机响应时阻塞
     */
    template <MotorParameter Param>
    auto GetParameter() -> MotorParameterRetType<Param>;

    /** @} */

private:
    std::optional<MotorStatus> GetStatusImpl(int life_cycle_deduction);
    void SetOnStatusImpl(std::function<void(const MotorStatus&)> callback);
    void OnMessage(const MotorPackMsg& message);
    void CancelWaiters() noexcept;
    void CancelWaitersWithoutDrain() noexcept;
    bool WaitersDrained() const noexcept;
    bool SendAndWaitErased(const MotorPackMsg& message,
                           std::function<bool(const MotorPackMsg&)> checker,
                           std::shared_ptr<void> content,
                           std::function<void(const MotorPackMsg&, void*)> writer,
                           std::chrono::milliseconds timeout = std::chrono::milliseconds{500});

    template <typename Result, typename Checker, typename Writer>
    std::optional<Result> SendAndWaitTyped(
        const MotorPackMsg& message, Checker checker, Writer writer,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{500}) {
        auto content = std::make_shared<Result>();
        const bool completed = SendAndWaitErased(
            message, std::move(checker), content,
            [writer = std::move(writer)](const MotorPackMsg& packet, void* raw_content) {
                writer(packet, *static_cast<Result*>(raw_content));
            },
            timeout);
        if (!completed) {
            return std::nullopt;
        }
        return *content;
    }

    std::optional<MotorPackMsg> SendAndWait(
        const MotorPackMsg& message, std::function<bool(const MotorPackMsg&)> checker,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{500});
    bool SendParameterAndWait(const MotorPackMsg& message, uint8_t parameter_id,
                              std::vector<uint8_t> expected_payload);
    void UpdateStatusDispatcher();
    void HandleStatus(const MotorStatus& status);
    void RecordCommand(const char* type, std::optional<float> kp, std::optional<float> kd,
                       std::optional<float> position, std::optional<float> speed,
                       std::optional<float> current, std::optional<float> torque,
                       std::optional<MotorStopMode> stop_mode, std::optional<bool> brake_enabled,
                       std::optional<int> feedback) noexcept;
    void DisableLogImpl(bool update_dispatcher);
    bool RecoverLogSession(const char* context) noexcept;
    void SetStatusLifeCycle(int max_life_cycle);
    void SetStatusMedianFilterWindowSizeImpl(std::size_t median_window_size);
    void SetStatusLimitFilterMaxDeltasImpl(const MotorStatus& limit_max_deltas);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos
