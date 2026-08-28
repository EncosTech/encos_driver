#pragma once

#include <array>
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

struct PmsStatus {
    std::array<bool, 6> v48_channel_enabled; /**< V48 通道 1-6 开关状态 */
    uint8_t battery_soc;                     /**< 电池 SOC（单位：%） */
    float battery_voltage;                   /**< 电池电压（单位：V） */
    float battery_current;                   /**< 电池电流（单位：A） */
    float v5_current;                        /**< V5 通道电流（单位：A） */
    std::array<float, 6> v48_currents;       /**< V48 通道 1-6 电流（单位：A） */
    std::array<float, 2> v19_currents;       /**< V19 通道 1-2 电流（单位：A） */
};

enum class PmsCommand : uint16_t {
    None = 0,
    DisableChannel1 = 1u << 0u,
    DisableChannel2 = 1u << 1u,
    DisableChannel3 = 1u << 2u,
    DisableChannel4 = 1u << 3u,
    DisableChannel5 = 1u << 4u,
    DisableChannel6 = 1u << 5u,
    EnableChannel1 = 1u << 8u,
    EnableChannel2 = 1u << 9u,
    EnableChannel3 = 1u << 10u,
    EnableChannel4 = 1u << 11u,
    EnableChannel5 = 1u << 12u,
    EnableChannel6 = 1u << 13u,
};

inline PmsCommand operator|(PmsCommand lhs, PmsCommand rhs) {
    return static_cast<PmsCommand>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
}

/**
 * @brief PMS 接口类，表示总线上的电源管理系统
 */
class ENCOS_BASE_API Pms {
    friend class EncosDriverManager;
    friend class DeviceStatusTestAccess;

private:
    explicit Pms(Bus* bus, LoggerPtr logger, std::function<void(const MotorPackMsg&)> writer);

public:
    ~Pms();

    /**
     * @brief 获取 PMS 状态快照
     * @return PMS 完整状态；任一状态帧超过 2 秒未更新时返回空
     */
    std::optional<PmsStatus> GetStatus();

    /**
     * @brief 设置 PMS 状态更新回调
     *
     * 回调由适配器接收线程同步调用，不应执行阻塞操作或耗时较长的工作。
     *
     * @param callback 状态回调，传入空函数可取消注册
     */
    void SetOnStatus(std::function<void(const PmsStatus&)> callback);

    /**
     * @brief 发送 PMS 通道控制命令
     * @param command 可按位组合的通道启停命令
     * @throws std::invalid_argument 同一通道同时启用和停用时抛出
     */
    void SendCommand(PmsCommand command);

private:
    /** @brief 在适配器接收线程中解码一帧 PMS 报告 */
    void OnMessage(const MotorPackMsg& message);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos
