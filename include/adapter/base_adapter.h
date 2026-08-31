#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "encos/export.h"
#include "motor/types.h"
#include "platform/log.h"

namespace encos {

class Bus;
class EncosDriverManager;
class FakeAdapterControl;
class Glove;
class RelayServer;
class BaseAdapterTestAccess;

/**
 * @brief 传输层适配器抽象基类（如 CAN、EtherCAT 等）
 *
 * `BaseAdapter` 仅负责底层 `MotorMessage` 的传输和接收入口；对象生命周期、
 * 已知总线和消息路由均由 `EncosDriverManager` 统一管理。
 * 具体的适配器必须实现 `Send` 和 `GetBuses` 方法。
 */
class ENCOS_BASE_API BaseAdapter {
public:
    friend class EncosDriverManager;
    friend class Bus;
    friend class Motor;
    friend class RelayServer;
    friend class BaseAdapterTestAccess;

    /**
     * @brief 获取（或创建）指定适配器索引的 `Bus` 实例
     * @param idx 对应的总线索引
     * @return 非拥有的 `Bus` 裸指针
     */
    Bus* GetBus(int idx = 0);

    /**
     * @brief 获取（或创建）指定从站和总线索引的 `Bus` 实例
     * @param slave_idx 网络中的从站索引
     * @param bus_idx 从站上的总线索引
     * @return 非拥有的 `Bus` 裸指针
     */
    Bus* GetBus(int slave_idx, int bus_idx);

    /**
     * @brief 获取（或创建）指定从站索引的 `Glove` 手套整手视图
     * @param slave_idx 网络中的手套从站索引
     * @return 非拥有的 `Glove` 裸指针
     *
     * 同一从站重复调用会返回同一存活对象。返回指针由驱动管理器拥有；调用
     * `DeleteGlove()` 后不得再使用。
     */
    Glove* GetGlove(int slave_idx);

    /**
     * @brief 返回此适配器管理的所有已知 `Bus` 实例
     * @return 总线索引到非拥有 `Bus` 裸指针的映射
     *
     * 实现应返回适配器总线的快照。
     */
    virtual std::unordered_map<int, Bus*> GetBuses() = 0;

    /**
     * @brief 检查适配器是否正常运行
     * @return 如果适配器正常返回 true，否则返回 false
     */
    virtual bool Ok() = 0;

    /**
     * @brief 查询 Fake 适配器控制能力
     * @return 如果适配器支持 Fake 控制则返回非空指针，否则返回空
     */
    virtual FakeAdapterControl* GetFakeAdapterControl();

    virtual ~BaseAdapter();

    /**
     * @brief 提交所有总线当前保留的消息并进入软同步模式
     *
     * 本函数只等待具体 Adapter 接收该批次，不等待物理链路发送或设备响应完成。
     * @warning 必须先完成 Bus 扫描、Motor/Battery/Pms 创建、设备初始化及同线程
     * 请求响应配置，再调用本函数进入软同步模式。本函数不会为这些操作自动提交。
     */
    void Commit();

    /**
     * @brief 批量设置所有总线的软同步发送模式
     * @param enabled true 时消息等待对应 Bus 或 Adapter Commit；false 时恢复直接提交
     * @warning 设置为 true 前必须完成 Bus 扫描、Motor/Battery/Pms 创建、设备初始化
     * 及同线程请求响应配置；软同步模式不会为这些操作自动提交。
     */
    void SetSyncMode(bool enabled);

    /**
     * @brief 获取适配器使用的网络接口名称
     * @return 接口名称字符串
     */
    std::string GetInterfaceName() const;

    /**
     * @brief 获取所有电机的最后已知状态
     * @return 电机唯一ID -> MotorStatus 的映射
     */
    std::map<std::int64_t, MotorStatus> GetMotorStatus();

    /**
     * @brief 获取指定电机的最后已知状态
     * @param bus_idx 总线索引
     * @param motor_idx 电机索引
     * @param life_cycle_deduction 本次读取扣减的生命周期值，<=0 时不扣减
     * @return 电机状态，如果不可用则返回空
     */
    std::optional<MotorStatus> GetMotorStatus(int bus_idx, int motor_idx,
                                              int life_cycle_deduction = 1);

    /**
     * @brief 设置该适配器下所有电机的状态缓存最大生命周期默认值
     * @param max_life_cycle 最大生命周期，INT_MAX 表示禁用生命周期清理
     *
     * 已创建的电机会立即应用，后续新创建的电机也会自动继承此默认值。
     * 若需对单个电机进行配置，请使用 `Motor::SetMaxStatusLifeCycle`。
     */
    void SetMaxStatusLifeCycle(int max_life_cycle);

    /**
     * @brief 获取该适配器的状态缓存最大生命周期默认值
     * @return 当前最大生命周期
     */
    int GetMaxStatusLifeCycle() const;

    /**
     * @brief 设置该适配器下所有电机的中值滤波窗口长度默认值
     * @param window_size 窗口长度；0 或 1 时关闭中值滤波，至少为 2
     * 时启用；每次设置都会清空已有中值窗口
     *
     * 已创建的电机会立即应用，后续新创建的电机也会自动继承此默认值。
     * 若需对单个电机进行配置，请使用 `Motor::SetStatusMedianFilterWindowSize`。
     */
    void SetStatusMedianFilterWindowSize(std::size_t window_size);

    /**
     * @brief 设置该适配器下所有电机的状态限幅滤波最大帧间变化默认值
     * @param position 位置最大变化；负值、NaN 或无穷大时关闭该量的限幅
     * @param speed 速度最大变化；负值、NaN 或无穷大时关闭该量的限幅
     * @param current 电流最大变化；负值、NaN 或无穷大时关闭该量的限幅
     * @param motor_temperature 电机温度最大变化；负值、NaN 或无穷大时关闭该量的限幅
     * @param mos_temperature MOS 温度最大变化；负值、NaN 或无穷大时关闭该量的限幅
     *
     * 已创建的电机会立即应用，后续新创建的电机也会自动继承此默认值。
     * 若需对单个电机进行配置，请使用 `Motor::SetStatusLimitFilterMaxDeltas`。
     */
    void SetStatusLimitFilterMaxDeltas(float position, float speed, float current,
                                       float motor_temperature, float mos_temperature);

    /**
     * @brief 为指定电机注册状态回调
     * @param bus_idx 总线索引
     * @param motor_idx 电机索引
     * @param callback 状态回调函数，传入 nullptr 可取消注册
     */
    void SetOnStatus(int bus_idx, int motor_idx, std::function<void(const MotorStatus&)> callback);

protected:
    /**
     * @brief 构造适配器基类
     * @param interface_name 传输接口名称
     * @param logger_name 日志记录器名称
     * @param log_level 日志过滤级别
     */
    BaseAdapter(const std::string& interface_name, const std::string& logger_name,
                LogLevel log_level = LogLevel::Info);

    /**
     * @brief 传输层实现收到新消息时调用
     * @param messages 接收到的消息集合
     *
     * 此方法将传入消息交由管理器执行已注册路由和总线未知帧投递。
     */
    void OnMessage(const MotorMessages& messages);

    /**
     * @brief 通过传输层发送底层电机消息
     * @param message 要发送的消息
     *
     * 具体适配器必须实现此方法以执行实际的 IO 操作。
     */
    virtual void Send(const MotorMessage& message) = 0;

    /**
     * @brief 批量发送底层电机消息
     * @param messages 要发送的消息集合
     *
     * 默认逐条调用单条发送入口，支持批量队列的适配器可重载此方法。
     */
    virtual void Send(const MotorMessages& messages);

    /**
     * @brief 提交需要保留边界的软同步批次
     * @param messages 要发送的完整批次
     *
     * 默认委托批量 Send；需要区分同步批次的周期型传输可重载此方法。
     */
    virtual void SendSynchronized(const MotorMessages& messages);

    /** @brief 获取该适配器使用的日志记录器 */
    LoggerPtr Logger() const;

    /**
     * @brief 获取已创建 Bus 的快照
     * @return 总线索引到 Bus 的映射
     */
    std::unordered_map<int, Bus*> GetKnownBusesSnapshot();

private:
    std::function<void(const MotorPackMsg&)> MakeDeviceWriter(Bus* bus);
    void InitializeBusSyncMode(Bus* bus, std::function<void()> publish);
    void SubmitDeviceMessage(Bus* bus, void* channel, const MotorMessage& message);
    void SubmitBusMessages(Bus* bus, const MotorMessages& messages);
    void CommitBus(Bus* bus);
    void SetBusSyncMode(Bus* bus, bool enabled);

    /**
     * @brief helper 核心使用的原始发送钩子，非公开 API
     * @param message 要转发的原始消息
     *
     * 仅通过 RelayServer 友元访问，调用具体适配器的 Send 实现。
     */
    void RelaySendRaw(const MotorMessage& message);

    /**
     * @brief helper 核心使用的原始批量发送钩子，非公开 API
     * @param messages 要转发的原始消息集合
     *
     * 仅通过 RelayServer 友元访问，调用具体适配器的批量 Send 实现。
     */
    void RelaySendRaw(const MotorMessages& messages);

    /**
     * @brief helper 核心使用的原始消息接收回调注册，非公开 API
     * @param callback 回调函数，传入空函数可取消注册
     *
     * 仅通过 RelayServer 友元访问。回调在接收线程中、路由和邮箱分类前同步触发；
     * 回调必须保持轻量且不得阻塞。
     */
    void SetRelayRawMessageCallback(std::function<void(const MotorMessages&)> callback);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** @brief 非拥有型适配器指针，生命周期由 EncosDriverManager 管理 */
using BaseAdapterPtr = BaseAdapter*;

}  // namespace encos
