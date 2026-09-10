#pragma once

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "adapter/base_adapter.h"
#include "adapter/base_adapter_impl.h"
#include "battery/battery.h"
#include "battery/battery_impl.h"
#include "bus/bus.h"
#include "bus/bus_impl.h"
#include "encos/driver_manager.h"
#include "glove/glove.h"
#include "glove/glove_calibrator.h"
#include "glove/glove_encoder.h"
#include "glove/glove_impl.h"
#include "imu/imu.h"
#include "imu/imu_impl.h"
#include "motor/motor.h"
#include "motor/motor_impl.h"
#include "motor/pack_helper.h"
#include "operation_gate.h"
#include "pms/pms.h"
#include "pms/pms_impl.h"
#include "protocol/route_ids.h"

namespace encos {

struct RouteRecord {
    void* device = nullptr;
    Bus* bus = nullptr;
    BaseAdapter* adapter = nullptr;
    EncosDriverManager::ReceiveCallback callback;
    platform::Mutex mutex;
    std::condition_variable_any condition;
    std::size_t in_flight = 0;
    bool retiring = false;
};

namespace driver_manager_internal {

enum class DeviceKind : std::uint8_t { Motor, Battery, Imu, Pms, GloveEncoder, GloveCalibrator };

inline uint8_t WithCanFdFlag(uint8_t frame_flags, bool canfd) {
    frame_flags = SanitizeCanFrameFlags(frame_flags);
    if (canfd) {
        return static_cast<uint8_t>(frame_flags | kCanFrameFlagFdMask);
    }
    return static_cast<uint8_t>(frame_flags & static_cast<uint8_t>(~kCanFrameFlagFdMask));
}

struct BusKey {
    BaseAdapter* adapter = nullptr;
    int raw_idx = 0;

    bool operator==(const BusKey& rhs) const noexcept {
        return adapter == rhs.adapter && raw_idx == rhs.raw_idx;
    }
};

struct BusKeyHash {
    std::size_t operator()(const BusKey& key) const noexcept {
        return std::hash<BaseAdapter*>{}(key.adapter) ^
               (std::hash<int>{}(key.raw_idx) + 0x9e3779b9u);
    }
};

/** @brief 手套整手 facade 的登记键：按（适配器, 从站号）唯一定位一只手套 */
struct GloveKey {
    BaseAdapter* adapter = nullptr;
    int slave_id = 0;

    bool operator==(const GloveKey& rhs) const noexcept {
        return adapter == rhs.adapter && slave_id == rhs.slave_id;
    }
};

struct GloveKeyHash {
    std::size_t operator()(const GloveKey& key) const noexcept {
        return std::hash<BaseAdapter*>{}(key.adapter) ^
               (std::hash<int>{}(key.slave_id) + 0x9e3779b9u);
    }
};

struct DeviceKey {
    Bus* bus = nullptr;
    DeviceKind kind = DeviceKind::Motor;
    int idx = 0;

    bool operator==(const DeviceKey& rhs) const noexcept {
        return bus == rhs.bus && kind == rhs.kind && idx == rhs.idx;
    }
};

struct DeviceKeyHash {
    std::size_t operator()(const DeviceKey& key) const noexcept {
        const auto bus_hash = std::hash<Bus*>{}(key.bus);
        const auto kind_hash = std::hash<unsigned>{}(static_cast<unsigned>(key.kind));
        return bus_hash ^ (kind_hash << 1u) ^ (std::hash<int>{}(key.idx) << 2u);
    }
};

struct RouteKey {
    BaseAdapter* adapter = nullptr;
    std::uint64_t unique_id = 0;

    bool operator==(const RouteKey& rhs) const noexcept {
        return adapter == rhs.adapter && unique_id == rhs.unique_id;
    }
};

struct RouteKeyHash {
    std::size_t operator()(const RouteKey& key) const noexcept {
        return std::hash<BaseAdapter*>{}(key.adapter) ^
               (std::hash<std::uint64_t>{}(key.unique_id) << 1u);
    }
};

/** @brief 叶子设备的类型化入口提供能力，生命周期引擎不解释协议。 */
struct DeviceRoute {
    EncosDriverManager::ReceiveCallback callback;
    EncosDriverManager::CancellationCallback cancel_waiters;
};
struct DeviceRecipe {
    DeviceKind kind;
    OperationKind operation_kind;
    std::vector<std::uint32_t> can_ids;
    std::function<void*()> construct;
    std::function<DeviceRoute(void*)> bind;
    std::function<void(void*)> initialize;
    std::function<void(void*)> commit;
    void (*destroy)(void*) noexcept;
};
struct RouteEntry {
    RouteKey key;
    std::shared_ptr<RouteRecord> record;
};
/** @brief 登记移出管理器后继续持有路由，直至所有回调排空。 */
struct DeviceRegistration {
    BaseAdapter* adapter = nullptr;
    Bus* bus = nullptr;
    OperationKind operation_kind = OperationKind::Motor;
    void (*destroy)(void*) noexcept = nullptr;
    std::vector<RouteEntry> routes;
    EncosDriverManager::CancellationCallback cancel_waiters;
};

inline std::int64_t MakeMotorStatusKey(int raw_bus_idx, int motor_idx) {
    return static_cast<std::int64_t>(EncosDriverManager::MakeReceiveUniqueId(
        raw_bus_idx, static_cast<std::uint32_t>(motor_idx)));
}

struct PendingCreation {
    std::condition_variable_any condition;
    bool complete = false;
    void* result = nullptr;
    std::exception_ptr error;
};

struct CallbackContext {
    void* device = nullptr;
    Bus* bus = nullptr;
    BaseAdapter* adapter = nullptr;
    bool raw_receive = false;
};

inline thread_local CallbackContext callback_context;

template <typename Key, typename Hash>
void* AwaitOrLead(platform::UniqueLock<platform::Mutex>& lock,
                  std::unordered_map<Key, std::shared_ptr<PendingCreation>, Hash>& pending,
                  const Key& key, std::shared_ptr<PendingCreation>& publication, bool& leader) {
    const auto it = pending.find(key);
    if (it == pending.end()) {
        publication = std::make_shared<PendingCreation>();
        pending.emplace(key, publication);
        leader = true;
        return nullptr;
    }
    publication = it->second;
    publication->condition.wait(lock, [&publication]() {
        return publication->complete;
    });
    if (publication->error) {
        std::rethrow_exception(publication->error);
    }
    return publication->result;
}

inline void CompletePending(const std::shared_ptr<PendingCreation>& pending, void* result,
                            std::exception_ptr error = {}) {
    pending->result = result;
    pending->error = std::move(error);
    pending->complete = true;
    pending->condition.notify_all();
}

}  // namespace driver_manager_internal

struct EncosDriverManager::Impl {
    struct MotorIndexReservation {
        Motor* motor = nullptr;
        std::function<void(const MotorStatus&)> callback;
        bool install_callback = false;
    };
    struct StatusConfig {
        int max_life_cycle = std::numeric_limits<int>::max();
        std::size_t median_window_size = 0;
        MotorStatus limit_max_deltas;
    };
    platform::Mutex object_mutex;
    OperationRegistry operation_registry;
    std::condition_variable_any child_creation_condition;
    std::condition_variable_any deletion_condition;
    std::unordered_map<std::string, BaseAdapter*> adapters;
    std::unordered_map<BaseAdapter*, std::string> adapter_names;
    std::unordered_map<driver_manager_internal::BusKey, Bus*, driver_manager_internal::BusKeyHash>
        buses;
    std::unordered_map<Bus*, driver_manager_internal::BusKey> bus_keys;
    /** @brief 仅能经 DestroyGlove 级联释放的手套内部总线 */
    std::unordered_set<Bus*> glove_internal_buses;
    /** @brief 正在创建的手套预留的帧内总线键，禁止公开销毁抢占 */
    std::unordered_set<driver_manager_internal::BusKey, driver_manager_internal::BusKeyHash>
        pending_glove_bus_keys;
    /** @brief 手套整手 facade 登记表（facade 非设备，仅簿记：幂等、销毁入口与适配器级联） */
    std::unordered_map<driver_manager_internal::GloveKey, Glove*,
                       driver_manager_internal::GloveKeyHash>
        gloves;
    std::unordered_map<Glove*, driver_manager_internal::GloveKey> glove_keys;
    std::unordered_map<driver_manager_internal::DeviceKey, void*,
                       driver_manager_internal::DeviceKeyHash>
        devices;
    std::unordered_map<void*, driver_manager_internal::DeviceKey> device_keys;
    std::unordered_map<void*, driver_manager_internal::DeviceKey> initializing_device_keys;
    std::unordered_map<driver_manager_internal::DeviceKey, MotorIndexReservation,
                       driver_manager_internal::DeviceKeyHash>
        motor_index_reservations;
    std::unordered_map<std::string, std::shared_ptr<driver_manager_internal::PendingCreation>>
        pending_adapters;
    std::unordered_map<driver_manager_internal::BusKey,
                       std::shared_ptr<driver_manager_internal::PendingCreation>,
                       driver_manager_internal::BusKeyHash>
        pending_buses;
    std::unordered_map<driver_manager_internal::DeviceKey,
                       std::shared_ptr<driver_manager_internal::PendingCreation>,
                       driver_manager_internal::DeviceKeyHash>
        pending_devices;
    /** @brief 手套 facade 创建并发去重（以 GloveKey 为键） */
    std::unordered_map<driver_manager_internal::GloveKey,
                       std::shared_ptr<driver_manager_internal::PendingCreation>,
                       driver_manager_internal::GloveKeyHash>
        pending_gloves;
    std::unordered_set<void*> deleting;
    std::unordered_set<Glove*> deleting_gloves;
    std::unordered_map<void*, Bus*> deleting_device_parents;
    std::unordered_map<Bus*, BaseAdapter*> deleting_bus_parents;
    std::unordered_map<void*, std::size_t> child_creations;
    std::unordered_set<Bus*> resetting_buses;
    std::unordered_map<BaseAdapter*, StatusConfig> adapter_status_configs;
    std::unordered_map<driver_manager_internal::RouteKey, std::function<void(const MotorStatus&)>,
                       driver_manager_internal::RouteKeyHash>
        status_callbacks;

    platform::Mutex route_mutex;
    std::unordered_map<driver_manager_internal::RouteKey, Motor*,
                       driver_manager_internal::RouteKeyHash>
        motor_route_reservations;
    std::unordered_map<void*, driver_manager_internal::DeviceRegistration> registrations;
    // 测试故障注入：仅用于锁内发布和父级计数的分配失败窗口。
    std::function<void(std::size_t)> route_publish_hook;
    std::function<void(void*)> child_creation_hook;
    std::function<void(std::size_t)> glove_activation_hook;

    ENCOS_BASE_API void* CreateDevice(Bus* bus, int idx,
                                      const driver_manager_internal::DeviceRecipe& recipe);
    ENCOS_BASE_API driver_manager_internal::DeviceRecipe MakeBatteryRecipe(Bus* bus, int idx);
    CreationHook creation_hook;
    DeletionHook deletion_hook;
    MigrationHook migration_hook;
    DeviceInitializerHook device_initializer_hook;
    std::function<void()> wait_hook;
    bool contain_deletion_hook_exceptions = false;
    bool deletion_hook_exception_observed = false;

    void ObserveWaitForTests() const {
        if (wait_hook) {
            wait_hook();
        }
    }

    void BeginChildCreationLocked(void* parent) {
        if (child_creation_hook) {
            child_creation_hook(parent);
        }
        ++child_creations[parent];
    }

    void EndChildCreation(void* parent) noexcept {
        platform::LockGuard<platform::Mutex> lock(object_mutex);
        const auto found = child_creations.find(parent);
        if (found == child_creations.end()) {
            return;
        }
        if (--found->second == 0) {
            child_creations.erase(found);
            child_creation_condition.notify_all();
        }
    }

    void WaitForChildCreations(void* parent) {
        platform::UniqueLock<platform::Mutex> lock(object_mutex);
        if (child_creations.find(parent) != child_creations.end()) {
            ObserveWaitForTests();
        }
        child_creation_condition.wait(lock, [this, parent]() {
            return child_creations.find(parent) == child_creations.end();
        });
    }

    void InvokeCreationHook(CreationStage stage) {
        CreationHook hook;
        {
            platform::LockGuard<platform::Mutex> lock(object_mutex);
            hook = creation_hook;
        }
        if (hook) {
            hook(stage);
        }
    }

    void InvokeDeviceInitializerHook(void* device) {
        DeviceInitializerHook hook;
        {
            platform::LockGuard<platform::Mutex> lock(object_mutex);
            hook = device_initializer_hook;
        }
        if (hook) {
            hook(device);
        }
    }

    void InvokeDeletionHook(DeletionStage stage) {
        DeletionHook hook;
        bool contain_exceptions = false;
        {
            platform::LockGuard<platform::Mutex> lock(object_mutex);
            hook = deletion_hook;
            contain_exceptions = contain_deletion_hook_exceptions;
        }
        if (hook) {
            try {
                hook(stage);
            } catch (...) {
                if (!contain_exceptions) {
                    throw;
                }
                platform::LockGuard<platform::Mutex> lock(object_mutex);
                deletion_hook_exception_observed = true;
            }
        }
    }

    void WaitForBusChildren(Bus* bus) {
        platform::UniqueLock<platform::Mutex> lock(object_mutex);
        ObserveWaitForTests();
        deletion_condition.wait(lock, [this, bus]() {
            return std::none_of(devices.begin(), devices.end(),
                                [bus](const auto& entry) {
                                    return entry.first.bus == bus;
                                }) &&
                   std::none_of(deleting_device_parents.begin(), deleting_device_parents.end(),
                                [bus](const auto& entry) {
                                    return entry.second == bus;
                                });
        });
    }

    void WaitForAdapterBuses(BaseAdapter* adapter) {
        platform::UniqueLock<platform::Mutex> lock(object_mutex);
        ObserveWaitForTests();
        deletion_condition.wait(lock, [this, adapter]() {
            return std::none_of(buses.begin(), buses.end(),
                                [adapter](const auto& entry) {
                                    return entry.first.adapter == adapter;
                                }) &&
                   std::none_of(deleting_bus_parents.begin(), deleting_bus_parents.end(),
                                [adapter](const auto& entry) {
                                    return entry.second == adapter;
                                });
        });
    }

    void WaitForBusDeviceDeletions(Bus* bus) {
        platform::UniqueLock<platform::Mutex> lock(object_mutex);
        ObserveWaitForTests();
        deletion_condition.wait(lock, [this, bus]() {
            return std::none_of(deleting_device_parents.begin(), deleting_device_parents.end(),
                                [bus](const auto& entry) {
                                    return entry.second == bus;
                                });
        });
    }

    StatusConfig GetStatusConfig(BaseAdapter* adapter) {
        platform::LockGuard<platform::Mutex> lock(object_mutex);
        return adapter_status_configs[adapter];
    }

    DeviceWriteFunction MakeWriter(Bus* bus) {
        BaseAdapter* adapter = nullptr;
        {
            platform::LockGuard<platform::Mutex> lock(object_mutex);
            const auto it = bus_keys.find(bus);
            if (it == bus_keys.end()) {
                throw std::invalid_argument("Bus is not registered");
            }
            adapter = it->second.adapter;
        }
        return adapter->MakeDeviceWriter(bus);
    }

    /** @brief 各 Motor 构造重载复用相同的接收与取消能力。 */
    static driver_manager_internal::DeviceRoute BindMotor(Motor* motor) {
        return {[motor](const MotorPackMsg& message) {
                    motor->OnMessage(message);
                },
                [motor]() {
                    motor->CancelWaiters();
                }};
    }

    /** @brief 手套组装激活复用叶子描述发布的键，避免再次解释协议。 */
    std::vector<std::uint32_t> RegisteredRouteIds(void* device) {
        platform::LockGuard<platform::Mutex> lock(route_mutex);
        std::vector<std::uint32_t> ids;
        for (const auto& entry : registrations.at(device).routes) {
            ids.push_back(static_cast<std::uint32_t>(entry.key.unique_id));
        }
        return ids;
    }

    /** @brief 调用者持有 manager 两把锁，内部锁序为 domain → RouteRecord。 */
    bool PublishRoutesLocked(void* device, BaseAdapter* adapter, Bus* bus,
                             const driver_manager_internal::DeviceRecipe& recipe,
                             driver_manager_internal::DeviceRoute binding) {
        using namespace driver_manager_internal;
        auto& domain = adapter->impl_->route_domain;
        platform::LockGuard<platform::Mutex> domain_lock(domain.mutex);
        DeviceRegistration registration{adapter,        bus, recipe.operation_kind,
                                        recipe.destroy, {},  std::move(binding.cancel_waiters)};
        registration.routes.reserve(recipe.can_ids.size());
        const int raw_idx = bus_keys.at(bus).raw_idx;
        for (const auto can_id : recipe.can_ids) {
            const RouteKey key{adapter, EncosDriverManager::MakeReceiveUniqueId(raw_idx, can_id)};
            if (domain.routes.count(key.unique_id) != 0 ||
                motor_route_reservations.count(key) != 0 ||
                std::any_of(registration.routes.begin(), registration.routes.end(),
                            [&key](const auto& entry) {
                                return entry.key == key;
                            })) {
                return false;
            }
            auto record = std::make_shared<RouteRecord>();
            record->device = device;
            record->bus = bus;
            record->adapter = adapter;
            record->callback = binding.callback;
            registration.routes.push_back({key, std::move(record)});
        }
        auto inserted = registrations.emplace(device, std::move(registration));
        if (!inserted.second) {
            return false;
        }
        std::size_t published = 0;
        try {
            for (const auto& entry : inserted.first->second.routes) {
                if (route_publish_hook) {
                    route_publish_hook(published);
                }
                domain.routes.emplace(entry.key.unique_id, entry.record);
                ++published;
            }
        } catch (...) {
            for (std::size_t i = 0; i < published; ++i) {
                domain.routes.erase(inserted.first->second.routes[i].key.unique_id);
            }
            registrations.erase(inserted.first);
            throw;
        }
        return true;
    }

    /** @brief 锁内摘出登记并封闭接收入口；移动登记不再分配回滚内存。 */
    driver_manager_internal::DeviceRegistration DetachRegistrationLocked(void* device) {
        const auto found = registrations.find(device);
        if (found == registrations.end()) {
            return {};
        }
        auto retired = std::move(found->second);
        registrations.erase(found);
        auto& domain = retired.adapter->impl_->route_domain;
        platform::LockGuard<platform::Mutex> domain_lock(domain.mutex);
        for (const auto& entry : retired.routes) {
            platform::LockGuard<platform::Mutex> route_lock(entry.record->mutex);
            entry.record->retiring = true;
            const auto active = domain.routes.find(entry.key.unique_id);
            if (active != domain.routes.end() && active->second == entry.record) {
                domain.routes.erase(active);
            }
        }
        return retired;
    }

    /** @brief 锁外取消等待并排空回调与公开方法，兼容尚未登记的创建失败。 */
    void DrainDevice(void* device, OperationKind kind, OperationGate* gate,
                     driver_manager_internal::DeviceRegistration& retired) {
        if (retired.cancel_waiters) {
            try {
                retired.cancel_waiters();
            } catch (...) {}
        }
        for (const auto& entry : retired.routes) {
            const auto& route = entry.record;
            platform::UniqueLock<platform::Mutex> lock(route->mutex);
            if (route->in_flight != 0) {
                ObserveWaitForTests();
            }
            route->condition.wait(lock, [&route]() {
                return route->in_flight == 0;
            });
        }
        if (gate != nullptr) {
            if (gate->HasActiveOperations()) {
                ObserveWaitForTests();
            }
            gate->WaitForDrain();
            operation_registry.ReclaimRetired(device, kind);
        }
    }
};

namespace driver_manager_internal {
/** @brief 仅绑定类型转换；创建、回滚和释放调度均由非模板引擎执行。 */
template <typename T, typename Factory, typename Binder, typename Initializer, typename Commit>
DeviceRecipe MakeDeviceRecipe(DeviceKind kind, OperationKind operation_kind,
                              std::vector<std::uint32_t> can_ids, Factory factory, Binder binder,
                              Initializer initializer, Commit commit) {
    return {kind,
            operation_kind,
            std::move(can_ids),
            [factory = std::move(factory)]() -> void* {
                return factory();
            },
            [binder = std::move(binder)](void* device) {
                return binder(static_cast<T*>(device));
            },
            [initializer = std::move(initializer)](void* device) {
                initializer(static_cast<T*>(device));
            },
            [commit = std::move(commit)](void* device) {
                commit(static_cast<T*>(device));
            },
            [](void* device) noexcept {
                delete static_cast<T*>(device);
            }};
}

template <typename T, typename Factory, typename Binder, typename Initializer, typename Commit>
T* CreateDeviceWithRoutes(EncosDriverManager::Impl* impl, Bus* bus, DeviceKind kind, int idx,
                          OperationKind operation_kind, std::vector<std::uint32_t> can_ids,
                          Factory factory, Binder binder, Initializer initializer, Commit commit) {
    auto recipe = MakeDeviceRecipe<T>(kind, operation_kind, std::move(can_ids), std::move(factory),
                                      std::move(binder), std::move(initializer), std::move(commit));
    return static_cast<T*>(impl->CreateDevice(bus, idx, recipe));
}
}  // namespace driver_manager_internal
}  // namespace encos
