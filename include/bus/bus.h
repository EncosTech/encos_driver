#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <unordered_map>

#include "encos/export.h"
#include "motor/types.h"
#include "platform/log.h"

namespace encos {

class Motor;
class BaseAdapter;
class Battery;
class EncosDriverManager;
class Imu;
class Pms;
class BusPortTestAccess;

/**
 * @brief CAN 总线抽象类，管理电机包装器和适配器 I/O
 *
 * `Bus` 类提供对 `Motor` 包装器的访问，能够扫描总线上连接的电机，
 * 并通过底层 `BaseAdapter` 发送/读取底层消息。
 */
class ENCOS_BASE_API Bus {
public:
    friend class EncosDriverManager;
    friend class BaseAdapter;
    friend class Motor;
    friend class Battery;
    friend class Imu;
    friend class Pms;
    friend class BusPortTestAccess;

    ~Bus();

    /**
     * @brief 获取（或创建）指定索引的 `Motor` 包装器，使用已知的 `MotorModel`
     * @param motor_idx 总线上的电机索引
     * @param model 已知的电机型号，用于初始化范围
     * @return 非拥有的 `Motor` 裸指针
     */
    Motor* GetMotor(int motor_idx, MotorModel model);

    /**
     * @brief 获取（或创建）指定索引的 `Motor` 包装器，使用已知型号和明确通信协议
     * @param motor_idx 总线上的电机索引
     * @param model 已知的电机型号，用于初始化范围
     * @param canfd 是否使用 CAN FD
     * @return 非拥有的 `Motor` 裸指针
     */
    Motor* GetMotor(int motor_idx, MotorModel model, bool canfd);

    /**
     * @brief 获取（或创建）指定索引的 `Motor` 包装器，使用已知的 `MotorPVTRanges`
     * @param motor_idx 总线上的电机索引
     * @param ranges 已知的电机 MotorPVTRanges，用于初始化范围
     * @return 非拥有的 `Motor` 裸指针
     */
    Motor* GetMotor(int motor_idx, MotorPVTRanges ranges);

    /**
     * @brief 获取（或创建）指定索引的 `Motor` 包装器，使用明确量程和通信协议
     * @param motor_idx 总线上的电机索引
     * @param ranges 已知的电机 MotorPVTRanges，用于初始化范围
     * @param canfd 是否使用 CAN FD
     * @return 非拥有的 `Motor` 裸指针
     */
    Motor* GetMotor(int motor_idx, MotorPVTRanges ranges, bool canfd);

    /**
     * @brief 获取（或创建）指定索引的 `Motor` 包装器
     *        通过查询电机固件进行初始化
     * @param motor_idx 总线上的电机索引
     * @return 非拥有的 `Motor` 裸指针
     */
    Motor* GetMotor(int motor_idx);

    /**
     * @brief 获取（或创建）指定索引的 `Motor` 包装器并使用明确通信协议查询固件
     * @param motor_idx 总线上的电机索引
     * @param canfd 是否使用 CAN FD
     * @return 非拥有的 `Motor` 裸指针
     */
    Motor* GetMotor(int motor_idx, bool canfd);

    /**
     * @brief 获取（或创建）指定索引的 `Battery` 包装器
     * @param battery_idx 电池设备地址（不含默认偏移值）
     * @return 非拥有的 `Battery` 裸指针
     */
    Battery* GetBattery(int battery_idx);

    /**
     * @brief 获取（或创建）指定索引的 `Imu` 包装器
     * @param imu_idx IMU 设备索引，0 对应默认源地址 0x59
     * @return 非拥有的 `Imu` 裸指针
     */
    Imu* GetImu(int imu_idx);

    /**
     * @brief 获取（或创建）当前总线的 `Pms` 包装器
     * @return 非拥有的 `Pms` 裸指针
     */
    Pms* GetPms();

    /**
     * @brief 选择指定索引的现有 `Motor` 包装器
     * @param motor_idx 总线上的电机索引
     * @return 非拥有的 `Motor` 裸指针，如果未找到则返回 nullptr
     *
     * 此方法不会创建新的 `Motor` 实例。仅返回已创建的现有实例。
     */
    Motor* SelectMotor(int motor_idx);

    /**
     * @brief 获取此总线上的所有现有电机
     * @return 电机索引 -> Motor 实例的映射
     *
     * 此方法不会创建新的 Motor 实例。仅返回已创建的现有实例。
     */
    std::unordered_map<int, Motor*> GetMotors();

    /**
     * @brief 检测当前总线是否存在外部设备流量
     * @return 如检测到外部设备流量则返回 true
     *
     * 此方法会消费当前总线上的未读缓冲区，等待观察窗口后按是否存在新输入更新外部设备标记。
     */
    bool DetectExternalDevice();

    /**
     * @brief 查询当前总线是否已被标记为外部设备总线
     * @return 如果已标记为外部设备总线则返回 true
     */
    bool HasExternalDevice() const;

    /**
     * @brief 扫描总线上的连接电机
     * @return 发现的电机索引到其包装器的映射
     *
     * 此例程先发送一轮普通 CAN 查询，再仅对其发现的 ID 发送 CAN FD 查询，并在发送发现
     * 数据包及等待响应时阻塞。任意 CAN FD 回包均优先确认该电机使用 CAN FD。
     * 它将发现结果与管理器中已有的电机合并并返回非拥有指针快照。
     */
    std::unordered_map<int, Motor*> ScanMotors();

    /**
     * @brief 将总线上的所有电机 ID 重置为 1
     * @warning 如果存在多个电机，这将导致 ID 冲突
     * @param wait_for_ack 如果为 true，等待确认包
     * @return 如果所有电机都确认重置命令则返回 true
     */
    bool ResetMotorsId(bool wait_for_ack = true);

    /**
     * @brief 提交当前总线已聚合的发送消息并进入软同步模式
     *
     * 本函数只等待具体 Adapter 接收当前总线的批次，不等待物理链路发送或设备响应完成。
     */
    void Commit();

    /**
     * @brief 设置当前总线的软同步发送模式
     * @param enabled true 时消息等待当前总线 Commit；false 时恢复直接发送
     */
    void SetSyncMode(bool enabled);

    /**
     * @brief 获取与此 `Bus` 关联的适配器总线索引
     * @return 适配器总线索引
     */
    int GetBusIndex() const;

private:
    explicit Bus(BaseAdapter* adapter, int idx, LoggerPtr logger);
    auto AcquireOperation();
    auto TryAcquireOperation();
    void SubmitDeviceMessage(void* channel, const MotorPackMsg& message);
    void UnregisterSendChannel(void* channel);

    /** @brief 取走当前总线未知 ID 邮箱中的全部帧 */
    MotorMessages DrainUnknownMessagesLocked();
    /** @brief 清空当前总线未知 ID 邮箱 */
    void ClearUnknownMessagesLocked();
    bool DetectExternalDeviceLocked();
    bool DetectExternalDeviceLockedImpl(
        const std::function<void(std::chrono::milliseconds)>& sleep_function);
    std::unordered_map<int, Motor*> ScanMotorsImpl();
    std::unordered_map<int, Motor*> ScanMotorsForTesting();
    bool DetectExternalDeviceLockedForTesting(
        const std::function<void(std::chrono::milliseconds)>& sleep_function);

    /**
     * @brief 向指定电机索引发送打包的 `MotorPackMsg`
     * @param idx 目标电机索引
     * @param message 要发送的打包电机消息
     */
    void Send(int idx, const MotorPackMsg& message);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos
