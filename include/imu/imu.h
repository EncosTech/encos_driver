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

/** @brief 三轴线加速度 */
struct ImuAcceleration {
    float x; /**< X 轴加速度（单位：m/s^2） */
    float y; /**< Y 轴加速度（单位：m/s^2） */
    float z; /**< Z 轴加速度（单位：m/s^2） */
};

/** @brief 三轴角速度 */
struct ImuAngularVelocity {
    float x; /**< X 轴角速度（单位：dps） */
    float y; /**< Y 轴角速度（单位：dps） */
    float z; /**< Z 轴角速度（单位：dps） */
};

/** @brief 欧拉角姿态 */
struct ImuEulerAngle {
    float pitch;   /**< 俯仰角（单位：deg） */
    float roll;    /**< 横滚角（单位：deg） */
    float heading; /**< 航向角（单位：deg） */
};

/** @brief 四元数姿态 */
struct ImuQuaternion {
    float qw; /**< 四元数 W 分量 */
    float qx; /**< 四元数 X 分量 */
    float qy; /**< 四元数 Y 分量 */
    float qz; /**< 四元数 Z 分量 */
};

/** @brief IMU 各类状态帧的最新聚合结果 */
struct ImuStatus {
    std::optional<ImuAcceleration> acceleration;        /**< 三轴加速度 */
    std::optional<ImuAngularVelocity> angular_velocity; /**< 三轴角速度 */
    std::optional<ImuEulerAngle> euler_angle;           /**< 欧拉角 */
    std::optional<ImuQuaternion> quaternion;            /**< 四元数 */
};

/**
 * @brief IMU 接口类，表示总线上的单个 YIS130 IMU
 *
 * 此类按 YIS130 SAE J1939 兼容扩展帧解码加速度、角速度、欧拉角和四元数。
 */
class ENCOS_BASE_API Imu {
    friend class EncosDriverManager;
    friend class DeviceStatusTestAccess;

private:
    explicit Imu(Bus* bus, uint16_t imu_idx, LoggerPtr logger,
                 std::function<void(const MotorPackMsg&)> writer);

public:
    ~Imu();

    /**
     * @brief 获取 IMU 状态快照
     * @return IMU 状态
     */
    ImuStatus GetStatus();

    /**
     * @brief 设置 IMU 状态更新回调
     *
     * 回调由适配器接收线程同步调用，不应执行阻塞操作或耗时较长的工作。
     *
     * @param callback 状态回调，传入空函数可取消注册
     */
    void SetOnStatus(std::function<void(const ImuStatus&)> callback);

    /**
     * @brief 获取原生平台 IMU 更新周期
     * @return 原生平台更新周期
     */
    static constexpr std::chrono::milliseconds NativeUpdateInterval() {
        return std::chrono::milliseconds(5);
    }

    /**
     * @brief 获取 JS/WASM 平台 IMU 更新周期
     * @return JS/WASM 平台更新周期
     */
    static constexpr std::chrono::milliseconds JsUpdateInterval() {
        return std::chrono::milliseconds(20);
    }

private:
    /** @brief 在适配器接收线程中解码一帧 IMU 报告 */
    void OnMessage(const MotorPackMsg& message);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos
