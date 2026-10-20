// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "device/motor/types.h"
#include "encos/export.h"
#include "external/json.hpp"
#include "platform/log.h"

namespace encos {

class BaseAdapter;
class Battery;
class Bus;
class Device;
class Glove;
class GloveCalibrator;
class GloveEncoder;
class Imu;
class Motor;
class OperationGate;
class Pms;
class DriverManagerTestAccess;

/**
 * @brief 统一拥有并管理全部驱动领域对象的进程级单例
 *
 * 方法按所属DLL分别导出：CreateAdapter由主库提供，其余方法由Base库提供。
 * 返回的裸指针均不转移所有权；成功删除父对象后，其整棵子树指针立即失效。
 * 删除会阻止新的 Bus/设备公有方法进入，并等待已经进入的公有方法、接收回调和等待者退出。
 * 调用方不得在删除成功后继续访问旧指针，也不得自行 `delete` 管理器返回的对象。
 */
class EncosDriverManager {
    friend class BaseAdapter;
    friend class Battery;
    friend class Bus;
    friend class Device;
    friend class Glove;
    friend class GloveCalibrator;
    friend class GloveEncoder;
    friend class Imu;
    friend class Motor;
    friend class Pms;
    friend class DriverManagerTestAccess;

public:
    struct Impl;
    /** @brief 创建适配器实例的工厂函数 */
    using AdapterFactory = std::function<BaseAdapter*()>;
    /** @brief 向设备发送一帧消息的函数 */
    using DeviceWriteFunction = std::function<void(const MotorPackMsg&)>;
    /** @brief 设备消息接收回调 */
    using ReceiveCallback = std::function<void(const MotorPackMsg&)>;
    /** @brief 等待操作取消回调 */
    using CancellationCallback = std::function<void()>;

    /** @cond INTERNAL */
    /** @brief 仅用于测试创建与父级删除竞争的对象发布阶段 */
    enum class CreationStage : std::uint8_t {
        BeforeBusPublish,
        BeforeDevicePublish,
        BeforeDeviceCommit,
        BeforeGloveFacadePublish,
        AfterGloveCallbacksConnected,
    };
    /** @brief 仅用于测试级联销毁顺序与并发的对象销毁阶段 */
    enum class DeletionStage : std::uint8_t {
        BeforeDeviceDestroy,
        BeforeBusDestroy,
        BeforeAdapterReceiveDrain,
        BeforeAdapterDestroy,
    };
    /** @brief 仅用于测试电机索引迁移中的异常回滚 */
    enum class MigrationStage : std::uint8_t { BeforeCurrentRangeMove };
    using CreationHook = std::function<void(CreationStage)>;
    using DeviceInitializerHook = std::function<void(void*)>;
    using DeletionHook = std::function<void(DeletionStage)>;
    using MigrationHook = std::function<void(MigrationStage)>;
    /** @endcond */

    /** @brief 获取进程级管理器单例 */
    ENCOS_BASE_API static EncosDriverManager& Instance();

    /**
     * @brief 通过插件创建或复用适配器
     */
    ENCOS_API BaseAdapter* CreateAdapter(const std::string& adapter_type,
                                         const std::string& interface_name,
                                         const std::string& logger_name = "",
                                         LogLevel log_level = LogLevel::Info);

    /**
     * @brief 通过插件创建或复用适配器并传入扩展配置
     * @param adapter_type 适配器类型名
     * @param interface_name 适配器唯一接口名
     * @param logger_name 日志记录器名称，为空时自动生成
     * @param log_level 日志过滤级别
     * @param extend 适配器自定义扩展配置；字段语义由插件自行定义
     * @return 非拥有的适配器裸指针
     * @throws std::runtime_error `extend` 不是 JSON 对象、插件缺失或插件构造失败
     *
     * 与四参数重载是两个独立符号，既有调用方无需重新编译。适配器身份仍只由
     * `interface_name` 决定：命中既有实例时，`extend` 中 `buses` 的条目会经
     * `BaseAdapter::UpdateBusExtend` 增量应用到该实例；顶层公共参数无法重新应用，
     * 会被忽略并记录诊断。返回的实例已处于更新完成后的状态。
     */
    ENCOS_API BaseAdapter* CreateAdapter(const std::string& adapter_type,
                                         const std::string& interface_name,
                                         const std::string& logger_name, LogLevel log_level,
                                         const nlohmann::json& extend);

    /**
     * @brief 接管工厂返回的适配器并按接口名并发去重
     * @param interface_name 适配器唯一接口名
     * @param factory 返回新裸指针的工厂；失败时由管理器回滚删除
     */
    ENCOS_BASE_API BaseAdapter* CreateAdapterWithFactory(const std::string& interface_name,
                                                         AdapterFactory factory);

    /**
     * @brief 同上，并携带用于增量更新既有实例的扩展配置串
     * @param interface_name 适配器唯一接口名
     * @param extend_json 宿主规范化的扩展配置串；空串表示没有更新请求
     * @param factory 返回新裸指针的工厂；失败时由管理器回滚删除
     * @return 新建的，或已按 `buses` 增量更新过的既有适配器裸指针
     * @throws std::runtime_error 插件构造失败，或既有实例的增量更新被拒绝
     *
     * 命中既有实例（含并发创建中的在飞实例）且 `extend_json` 非空时，调用该实例的
     * `BaseAdapter::UpdateBusExtend`。更新在对象锁之外、以适配器自身的在飞引用保护下
     * 串行执行。生命周期准入在对象锁内完成，销毁会等待在飞更新退出。
     */
    ENCOS_BASE_API BaseAdapter* CreateAdapterWithFactory(const std::string& interface_name,
                                                         const std::string& extend_json,
                                                         AdapterFactory factory);

    /** @brief 获取或创建指定适配器下的总线 */
    ENCOS_BASE_API Bus* CreateBus(BaseAdapter* adapter, int raw_bus_idx);
    /** @brief 获取或创建使用已知型号的电机 */
    ENCOS_BASE_API Motor* CreateMotor(Bus* bus, int motor_idx, MotorModel model,
                                      uint8_t frame_flags = 0);
    /** @brief 获取或创建使用已知型号及明确 CAN FD 属性的电机 */
    ENCOS_BASE_API Motor* CreateMotor(Bus* bus, int motor_idx, MotorModel model,
                                      uint8_t frame_flags, bool canfd);
    /** @brief 获取或创建使用明确量程的电机 */
    ENCOS_BASE_API Motor* CreateMotor(Bus* bus, int motor_idx, MotorProperties ranges,
                                      uint8_t frame_flags = 0);
    /** @brief 获取或创建使用明确量程及 CAN FD 属性的电机 */
    ENCOS_BASE_API Motor* CreateMotor(Bus* bus, int motor_idx, MotorProperties ranges,
                                      uint8_t frame_flags, bool canfd);
    /** @brief 获取或创建并从固件初始化电机 */
    ENCOS_BASE_API Motor* CreateMotor(Bus* bus, int motor_idx, uint8_t frame_flags = 0);
    /** @brief 获取或创建并按明确 CAN FD 属性从固件初始化电机 */
    ENCOS_BASE_API Motor* CreateMotor(Bus* bus, int motor_idx, uint8_t frame_flags, bool canfd);
    /** @brief 获取或创建电池设备 */
    ENCOS_BASE_API Battery* CreateBattery(Bus* bus, int battery_idx);
    /** @brief 获取或创建惯导设备 */
    ENCOS_BASE_API Imu* CreateImu(Bus* bus, int imu_idx);
    /** @brief 获取或创建电源管理设备 */
    ENCOS_BASE_API Pms* CreatePms(Bus* bus);
    /** @brief 获取或创建指定从站的手套整手视图 */
    ENCOS_BASE_API Glove* CreateGlove(BaseAdapter* adapter, int slave_id);
    /**
     * @brief 获取或创建由外部设备插件提供的设备
     * @param bus 设备所属总线
     * @param device_type 设备类型名（如 "TestDevice"）
     * @param config 设备自定义配置 JSON；未传入时为空对象 `{}`
     * @return 非拥有的 `Device` 裸指针
     * @throws std::runtime_error 设备插件缺失、ABI 不匹配或能力位与路由不一致
     *
     * 设备身份为 `(bus, type, config)`：配置规范化相等时复用既有实例，不同配置创建独立实例。
     */
    ENCOS_BASE_API Device* CreateDevice(Bus* bus, const std::string& device_type,
                                        const nlohmann::json& config = nlohmann::json::object());

    /** @brief 查询已存在的电机且不创建对象 */
    ENCOS_BASE_API Motor* FindMotor(Bus* bus, int motor_idx) const;
    /** @brief 获取总线下电机的非拥有快照 */
    ENCOS_BASE_API std::unordered_map<int, Motor*> GetMotors(Bus* bus) const;
    /** @brief 获取适配器下总线的非拥有快照 */
    ENCOS_BASE_API std::unordered_map<int, Bus*> GetBuses(BaseAdapter* adapter) const;

    /** @brief 在确认设备应答后迁移电机索引和接收路由 */
    ENCOS_BASE_API bool MigrateMotorIndex(Motor* motor, int new_motor_idx);

    /** @brief 删除适配器及完整子树 */
    ENCOS_BASE_API bool DestroyAdapter(BaseAdapter* adapter);
    /** @brief 按接口名删除适配器及完整子树 */
    ENCOS_BASE_API bool DestroyAdapterByInterfaceName(const std::string& interface_name);
    /** @brief 删除总线及完整设备子树 */
    ENCOS_BASE_API bool DestroyBus(Bus* bus);
    /** @brief 删除电机 */
    ENCOS_BASE_API bool DestroyMotor(Motor* motor);
    /** @brief 删除电池 */
    ENCOS_BASE_API bool DestroyBattery(Battery* battery);
    /** @brief 删除惯导设备 */
    ENCOS_BASE_API bool DestroyImu(Imu* imu);
    /** @brief 删除电源管理设备 */
    ENCOS_BASE_API bool DestroyPms(Pms* pms);
    /** @brief 删除外部设备插件提供的设备 */
    ENCOS_BASE_API bool DestroyDevice(Device* device);
    /**
     * @brief 删除管理器拥有的手套整手视图
     * @param glove 由 `CreateGlove()` 或 `BaseAdapter::GetGlove()` 返回的指针
     * @return 删除是否完整完成；传入空指针、非托管对象、重复销毁或回调中销毁时返回 false
     *
     * 调用后不得再访问传入指针。
     */
    ENCOS_BASE_API bool DestroyGlove(Glove* glove);

    /**
     * @brief 分发一帧到已登记设备并吞掉回调异常
     * @return 命中路由时返回 true
     */
    ENCOS_BASE_API bool DispatchReceive(BaseAdapter* adapter, int raw_bus_idx,
                                        const MotorPackMsg& message);

    /**
     * @brief 将未注册帧投递到其总线专属邮箱
     * @return 目标总线存在时返回 true，否则返回 false
     */
    ENCOS_BASE_API bool DispatchUnknownReceive(BaseAdapter* adapter, int raw_bus_idx,
                                               const MotorPackMsg& message) noexcept;

    /** @brief 生成无有符号左移未定义行为的总线/CAN 唯一 ID */
    static constexpr std::uint64_t MakeReceiveUniqueId(int raw_bus_idx,
                                                       std::uint32_t can_id) noexcept {
        return (std::uint64_t{static_cast<std::uint32_t>(raw_bus_idx)} << 32u) |
               std::uint64_t{can_id};
    }

private:
    /** @brief 析构与测试复位时删除全部托管对象 */
    ENCOS_BASE_API void DestroyAllManagedObjects();
    /** @brief 仅供整手销毁路径级联释放手套内部总线 */
    ENCOS_BASE_API bool DestroyGloveInternalBus(Bus* bus);
    /** @brief 按内部归属原子判定后销毁总线 */
    ENCOS_BASE_API bool DestroyBusImpl(Bus* bus, bool allow_glove_internal);
    ENCOS_BASE_API bool RegisterReceiveRoutes(void* device, BaseAdapter* adapter, Bus* bus,
                                              const std::vector<std::uint32_t>& can_ids,
                                              ReceiveCallback callback,
                                              CancellationCallback cancel_waiters = {});
    /** @brief 把一帧投递给本总线的整总线认领设备；未认领或认领项已退役时不投递 */
    ENCOS_BASE_API void DeliverToClaimRoute(BaseAdapter* adapter, int raw_bus_idx,
                                            const MotorPackMsg& message);
    ENCOS_BASE_API void ResetForTests();
    ENCOS_BASE_API void SetCreationHookForTests(CreationHook hook);
    ENCOS_BASE_API void SetDeviceInitializerHookForTests(DeviceInitializerHook hook);
    ENCOS_BASE_API void SetDeletionHookForTests(DeletionHook hook);
    ENCOS_BASE_API void SetMigrationHookForTests(MigrationHook hook);
    ENCOS_BASE_API void SetWaitHookForTests(std::function<void()> hook);
    ENCOS_BASE_API void RunWithSlowPathLocksForTests(const std::function<void()>& callback);

public:
    EncosDriverManager(const EncosDriverManager&) = delete;
    EncosDriverManager& operator=(const EncosDriverManager&) = delete;

private:
    class ENCOS_BASE_API DeviceOperation {
    public:
        DeviceOperation(const DeviceOperation&) = delete;
        DeviceOperation& operator=(const DeviceOperation&) = delete;
        DeviceOperation(DeviceOperation&&) = delete;
        DeviceOperation& operator=(DeviceOperation&&) = delete;
        ~DeviceOperation();
        explicit operator bool() const noexcept {
            return gate_ != nullptr;
        }

    private:
        friend class EncosDriverManager;
        DeviceOperation(OperationGate* gate, bool required);

        /** @brief 当前线程的活动操作栈顶；改用函数内 thread_local 以满足 MSVC 的 DLL 接口限制 */
        static DeviceOperation*& ActiveHead();
        OperationGate* gate_ = nullptr;
        DeviceOperation* previous_ = nullptr;
        bool owns_admission_ = false;
    };

    /** @brief 为设备公有方法登记在飞调用，设备退役后抛出异常 */
    ENCOS_BASE_API DeviceOperation AcquireDeviceOperation(Motor* device);
    ENCOS_BASE_API DeviceOperation AcquireDeviceOperation(Battery* device);
    ENCOS_BASE_API DeviceOperation AcquireDeviceOperation(Imu* device);
    ENCOS_BASE_API DeviceOperation AcquireDeviceOperation(Pms* device);
    ENCOS_BASE_API DeviceOperation AcquireDeviceOperation(Device* device);
    ENCOS_BASE_API DeviceOperation TryAcquireGloveOperation(Glove* glove) noexcept;
    ENCOS_BASE_API DeviceOperation AcquireDeviceOperation(GloveEncoder* device);
    ENCOS_BASE_API DeviceOperation AcquireDeviceOperation(GloveCalibrator* device);

    ENCOS_BASE_API std::map<std::int64_t, MotorStatus> GetAdapterMotorStatus(
        BaseAdapter* adapter) const;
    ENCOS_BASE_API std::optional<MotorStatus> GetAdapterMotorStatus(BaseAdapter* adapter,
                                                                    int raw_bus_idx, int motor_idx,
                                                                    int life_cycle_deduction);
    ENCOS_BASE_API void ConfigureAdapterStatusLifeCycle(BaseAdapter* adapter, int max_life_cycle);
    ENCOS_BASE_API int GetAdapterStatusLifeCycle(BaseAdapter* adapter) const;
    ENCOS_BASE_API void ConfigureAdapterStatusMedianFilter(BaseAdapter* adapter,
                                                           std::size_t window_size);
    ENCOS_BASE_API void ConfigureAdapterStatusLimitFilter(BaseAdapter* adapter,
                                                          const MotorStatus& max_deltas);
    ENCOS_BASE_API void ConfigureAdapterStatusCallback(
        BaseAdapter* adapter, int raw_bus_idx, int motor_idx,
        std::function<void(const MotorStatus&)> callback);
    ENCOS_BASE_API void ApplyMotorStatusConfigurationLocked(Bus* bus, int motor_idx, Motor* motor);
    /** @brief 在扫描中以对象锁保护已发现电机的帧标志或创建新电机 */
    ENCOS_BASE_API Motor* ReconcileDiscoveredMotor(Bus* bus, int motor_idx, uint8_t frame_flags);
    /** @brief 按扫描判定的 CAN FD 属性更新或创建电机 */
    ENCOS_BASE_API Motor* ReconcileDiscoveredMotor(Bus* bus, int motor_idx, uint8_t frame_flags,
                                                   bool canfd);
    /** @brief 广播重置确认后将同总线电机对象收敛到 ID 1 */
    ENCOS_BASE_API bool ResetBusMotorsToIdOne(Bus* bus);
    ENCOS_BASE_API bool ReserveMotorIndex(Motor* motor, int new_motor_idx);
    ENCOS_BASE_API void ReleaseMotorIndexReservation(Motor* motor, int new_motor_idx) noexcept;
    ENCOS_BASE_API bool HasRegisteredExternalDevice(Bus* bus) const;
    ENCOS_BASE_API bool IsBusRegistered(BaseAdapter* adapter, int raw_bus_idx) const;
    /** @brief 查询总线对象是否仍登记在册（已销毁的总线返回 false） */
    ENCOS_BASE_API bool IsBusAlive(Bus* bus) const;
    ENCOS_BASE_API DeviceOperation AcquireBusOperation(Bus* bus);
    ENCOS_BASE_API DeviceOperation TryAcquireBusOperation(Bus* bus) noexcept;

    /** @brief 登记适配器接收回调的生命周期，供 BaseAdapter 内部使用 */
    ENCOS_BASE_API DeviceOperation TryAcquireAdapterReceive(BaseAdapter* adapter) noexcept;
    /** @brief 在适配器接收上下文中调用原始中继回调，供 BaseAdapter 内部使用 */
    ENCOS_BASE_API void DispatchRawReceiveCallback(
        BaseAdapter* adapter, int raw_bus_idx,
        const std::function<void(const MotorMessages&)>& callback,
        const MotorMessages& messages) noexcept;

    ENCOS_BASE_API EncosDriverManager();
    ENCOS_BASE_API ~EncosDriverManager() noexcept;

    Impl* impl_;
};

}  // namespace encos
