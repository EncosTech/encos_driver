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

inline OperationKind ToOperationKind(DeviceKind kind) {
    switch (kind) {
        case DeviceKind::Motor:
            return OperationKind::Motor;
        case DeviceKind::Battery:
            return OperationKind::Battery;
        case DeviceKind::Imu:
            return OperationKind::Imu;
        case DeviceKind::Pms:
            return OperationKind::Pms;
        case DeviceKind::GloveEncoder:
            return OperationKind::GloveEncoder;
        case DeviceKind::GloveCalibrator:
            return OperationKind::GloveCalibrator;
    }
    std::terminate();
}

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
    std::unordered_map<driver_manager_internal::RouteKey, std::shared_ptr<RouteRecord>,
                       driver_manager_internal::RouteKeyHash>
        routes;
    std::unordered_map<driver_manager_internal::RouteKey, Motor*,
                       driver_manager_internal::RouteKeyHash>
        motor_route_reservations;
    std::unordered_map<void*, std::vector<driver_manager_internal::RouteKey>> device_routes;
    std::unordered_map<void*, CancellationCallback> cancellations;
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

    std::vector<std::uint32_t> RouteIds(driver_manager_internal::DeviceKind kind, int idx) const {
        using driver_manager_internal::DeviceKind;
        switch (kind) {
            case DeviceKind::Motor:
                return {static_cast<std::uint32_t>(idx)};
            case DeviceKind::Battery: {
                const auto ids = protocol::BatteryStatusIds(static_cast<std::uint16_t>(idx));
                return {ids.begin(), ids.end()};
            }
            case DeviceKind::Imu: {
                const auto ids = protocol::ImuStatusIds(static_cast<std::uint16_t>(idx));
                return {ids.begin(), ids.end()};
            }
            case DeviceKind::Pms:
                return {protocol::kPmsStatusIds.begin(), protocol::kPmsStatusIds.end()};
            case DeviceKind::GloveEncoder:
                // 编码器设备只注册本编码器的角度报告；idx 为全局编码器号 0-49
                // （手指号×10+编码器号），路由挂在各自帧内总线上，互不冲突。
                return {protocol::kGloveEncoderBaseId + static_cast<std::uint32_t>(idx)};
            case DeviceKind::GloveCalibrator:
                // 虚拟校准设备注册本分区唯一的校准响应路由；idx 为手指号 0-4。
                return {protocol::kGloveCalibrationId};
        }
        return {};
    }

    bool PublishRoutesLocked(void* device, BaseAdapter* adapter, Bus* bus,
                             const std::vector<std::uint32_t>& can_ids, ReceiveCallback callback,
                             CancellationCallback cancel_waiters) {
        auto& domain = adapter->impl_->route_domain;
        platform::LockGuard<platform::Mutex> domain_lock(domain.mutex);
        const int raw_idx = bus_keys.at(bus).raw_idx;
        std::vector<driver_manager_internal::RouteKey> keys;
        keys.reserve(can_ids.size());
        for (const auto can_id : can_ids) {
            driver_manager_internal::RouteKey key{
                adapter, EncosDriverManager::MakeReceiveUniqueId(raw_idx, can_id)};
            if (routes.find(key) != routes.end() ||
                motor_route_reservations.find(key) != motor_route_reservations.end()) {
                return false;
            }
            if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
                return false;
            }
            keys.push_back(key);
        }
        std::vector<std::shared_ptr<RouteRecord>> records;
        records.reserve(keys.size());
        for (std::size_t index = 0; index < keys.size(); ++index) {
            auto route = std::make_shared<RouteRecord>();
            route->device = device;
            route->bus = bus;
            route->adapter = adapter;
            route->callback = callback;
            records.push_back(std::move(route));
        }
        routes.reserve(routes.size() + keys.size());
        domain.routes.reserve(domain.routes.size() + keys.size());
        device_routes.reserve(device_routes.size() + 1);
        cancellations.reserve(cancellations.size() + 1);

        std::size_t published = 0;
        try {
            for (; published < keys.size(); ++published) {
                routes.emplace(keys[published], records[published]);
                try {
                    if (!domain.routes.emplace(keys[published].unique_id, records[published])
                             .second) {
                        routes.erase(keys[published]);
                        throw std::runtime_error("Adapter route already registered");
                    }
                } catch (...) {
                    routes.erase(keys[published]);
                    throw;
                }
            }
            device_routes.emplace(device, keys);
            cancellations.emplace(device, std::move(cancel_waiters));
            return true;
        } catch (...) {
            for (std::size_t index = 0; index < published; ++index) {
                routes.erase(keys[index]);
                domain.routes.erase(keys[index].unique_id);
            }
            device_routes.erase(device);
            cancellations.erase(device);
            throw;
        }
    }

    std::vector<std::shared_ptr<RouteRecord>> RetireRoutes(void* device,
                                                           CancellationCallback& cancel) {
        std::vector<std::shared_ptr<RouteRecord>> retired;
        platform::LockGuard<platform::Mutex> lock(route_mutex);
        const auto keys_it = device_routes.find(device);
        if (keys_it != device_routes.end()) {
            retired.reserve(keys_it->second.size());
            for (const auto& key : keys_it->second) {
                const auto route_it = routes.find(key);
                if (route_it == routes.end()) {
                    continue;
                }
                {
                    platform::LockGuard<platform::Mutex> route_lock(route_it->second->mutex);
                    route_it->second->retiring = true;
                }
                retired.push_back(route_it->second);
                {
                    auto& domain = route_it->second->adapter->impl_->route_domain;
                    platform::LockGuard<platform::Mutex> domain_lock(domain.mutex);
                    domain.routes.erase(key.unique_id);
                }
                routes.erase(route_it);
            }
            device_routes.erase(keys_it);
        }
        const auto cancel_it = cancellations.find(device);
        if (cancel_it != cancellations.end()) {
            cancel = std::move(cancel_it->second);
            cancellations.erase(cancel_it);
        }
        return retired;
    }
};

}  // namespace encos
