#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "encos/export.h"
#include "motor/types.h"
#include "platform/log.h"

namespace encos {

class Bus;
class EncosDriverManager;
class DeviceStatusTestAccess;

/** @brief 电池电量、电压及充放电能力状态 */
struct BatteryState {
    bool is_master;                  /**< 是否为主电池 */
    float soc;                       /**< 电池剩余电量百分比（值域 0-1） */
    float voltage;                   /**< 电池电压（单位：V） */
    float allowed_discharge_current; /**< 允许放电电流（单位：A） */
    float allowed_charge_current;    /**< 允许充电电流（单位：A） */
};

/** @brief 电池温度与充放电电流状态 */
struct BatteryTemp {
    float battery;           /**< 电芯温度（单位：°C） */
    float mos;               /**< 主MOS温度（单位：°C） */
    float discharge_current; /**< 当前放电电流（单位：A） */
    float charge_current;    /**< 当前充电电流（单位：A） */
};

/** @brief 电池故障位集合 */
struct BatteryError {
    bool could_not_charge;      /**< 无法充电错误 */
    bool could_not_discharge;   /**< 无法放电错误 */
    bool low_battery;           /**< 电量过低错误 */
    bool over_current_steady;   /**< 持续过流错误 */
    bool over_current_peak;     /**< 峰值过流错误 */
    bool over_current_charge;   /**< 充电过流错误 */
    bool battery_over_temp;     /**< 电芯过温错误 */
    bool mos_over_temp;         /**< MOS过温错误 */
    bool could_not_communicate; /**< 无法通信错误 */
    bool stopped_emergency;     /**< 紧急停止错误 */
    bool charger_fault;         /**< 充电器故障 */
    bool comm_timeout;          /**< 通信超时错误 */

    /** @brief 判断是否存在任一故障 */
    bool AnyError() const {
        return could_not_charge || could_not_discharge || low_battery || over_current_steady ||
               over_current_peak || over_current_charge || battery_over_temp || mos_over_temp ||
               could_not_communicate || stopped_emergency || charger_fault || comm_timeout;
    }
};

/** @brief BMS 主动上报的控制状态位 */
struct BatteryActiveCommands {
    bool shutdown_request;         /**< 关机请求 */
    bool discharge_request;        /**< 放电请求 */
    bool force_shutdown_broadcast; /**< 强制关机广播 */
    bool allow_charging;           /**< 允许充电 */
    bool fault_shutdown_broadcast; /**< 故障停机广播 */
    bool mos_status;               /**< MOS 状态 */
};

/** @brief 要发送给 BMS 的被动控制命令位 */
struct BatteryPassiveCommands {
    bool allow_shutdown = false;             /**< 允许关机 */
    bool allow_discharge = false;            /**< 允许放电 */
    bool parallel_discharge = false;         /**< 并机放电 */
    bool force_shutdown = false;             /**< 强制关机 */
    bool request_charging = false;           /**< 请求充电 */
    bool fault_shutdown_broadcast = false;   /**< 故障停机广播 */
    bool configure_fault_thresholds = false; /**< 配置故障阈值 */
    bool clear_fault = false;                /**< 清除故障 */
    bool factory_mode = false;               /**< 工厂模式 */
    bool debug = false;                      /**< 调试输出 */
};

/** @brief 电池各类状态帧的最新聚合结果 */
struct BatteryStatus {
    std::optional<BatteryState> state;                    /**< 电池状态数据 */
    std::optional<BatteryTemp> temp;                      /**< 电池温度与电流数据 */
    std::optional<BatteryActiveCommands> active_commands; /**< BMS 主动状态位 */
    BatteryError error;                                   /**< 电池错误状态 */
};

/**
 * @brief 电池接口类，表示总线上的单个电池
 *
 * 此类提供获取电池状态的方法。
 */
class ENCOS_BASE_API Battery {
    friend class EncosDriverManager;
    friend class DeviceStatusTestAccess;

private:
    explicit Battery(Bus* bus, uint16_t battery_idx, LoggerPtr logger,
                     std::function<void(const MotorPackMsg&)> writer);

public:
    ~Battery();

    /**
     * @brief 获取电池状态
     * @return 电池状态
     */
    BatteryStatus GetStatus();

    /**
     * @brief 设置电池状态更新回调
     *
     * 每个通过协议长度校验并完成解码的状态帧都会触发一次回调，
     * 即使该帧的值与上一帧完全相同。回调由适配器接收线程在状态锁外同步调用，
     * 不应执行阻塞操作或耗时较长的工作。
     *
     * @param callback 状态回调，传入空函数可取消注册
     */
    void SetOnStatus(std::function<void(const BatteryStatus&)> callback);

    /**
     * @brief 发送被动控制命令位域
     * @param commands 要发送的控制命令
     */
    void SendPassiveCommands(const BatteryPassiveCommands& commands);

    /**
     * @brief 发送清除故障命令
     */
    void ClearFault();

    /**
     * @brief 设置请求充电标志
     * @param enabled 是否请求充电
     */
    void RequestCharging(bool enabled);

    /**
     * @brief 设置允许放电标志
     * @param enabled 是否允许放电
     */
    void AllowDischarge(bool enabled);

private:
    /** @brief 在适配器接收线程中解码一帧电池报告 */
    void OnMessage(const MotorPackMsg& message);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos
