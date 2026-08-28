#include "driver_manager_impl.h"

namespace encos {

using driver_manager_internal::DeviceKind;

Motor* EncosDriverManager::FindMotor(Bus* bus, int motor_idx) const {
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    const auto parent = impl_->bus_keys.find(bus);
    if (parent == impl_->bus_keys.end() || impl_->deleting.count(bus) != 0 ||
        impl_->deleting.count(parent->second.adapter) != 0) {
        return nullptr;
    }
    const auto found = impl_->devices.find({bus, DeviceKind::Motor, motor_idx});
    return found == impl_->devices.end() || impl_->deleting.count(found->second) != 0
               ? nullptr
               : static_cast<Motor*>(found->second);
}

std::unordered_map<int, Motor*> EncosDriverManager::GetMotors(Bus* bus) const {
    std::unordered_map<int, Motor*> result;
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    const auto parent = impl_->bus_keys.find(bus);
    if (parent == impl_->bus_keys.end() || impl_->deleting.count(bus) != 0 ||
        impl_->deleting.count(parent->second.adapter) != 0) {
        return result;
    }
    for (const auto& [key, value] : impl_->devices) {
        if (key.bus == bus && key.kind == DeviceKind::Motor && impl_->deleting.count(value) == 0) {
            result.emplace(key.idx, static_cast<Motor*>(value));
        }
    }
    return result;
}

std::map<std::int64_t, MotorStatus> EncosDriverManager::GetAdapterMotorStatus(
    BaseAdapter* adapter) const {
    std::map<std::int64_t, MotorStatus> result;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        if (impl_->adapter_names.find(adapter) == impl_->adapter_names.end() ||
            impl_->deleting.count(adapter) != 0) {
            return result;
        }
        for (const auto& [key, value] : impl_->devices) {
            if (key.kind != DeviceKind::Motor) {
                continue;
            }
            const auto bus_it = impl_->bus_keys.find(key.bus);
            if (bus_it == impl_->bus_keys.end() || bus_it->second.adapter != adapter) {
                continue;
            }
            if (const auto status = static_cast<Motor*>(value)->GetStatusImpl(0)) {
                result.emplace(
                    driver_manager_internal::MakeMotorStatusKey(bus_it->second.raw_idx, key.idx),
                    *status);
            }
        }
    }
    return result;
}

std::optional<MotorStatus> EncosDriverManager::GetAdapterMotorStatus(BaseAdapter* adapter,
                                                                     int raw_bus_idx, int motor_idx,
                                                                     int life_cycle_deduction) {
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    if (impl_->adapter_names.find(adapter) == impl_->adapter_names.end() ||
        impl_->deleting.count(adapter) != 0) {
        return std::nullopt;
    }
    {
        const auto bus_it = impl_->buses.find({adapter, raw_bus_idx});
        if (bus_it == impl_->buses.end()) {
            return std::nullopt;
        }
        const auto device_it = impl_->devices.find({bus_it->second, DeviceKind::Motor, motor_idx});
        if (device_it == impl_->devices.end()) {
            return std::nullopt;
        }
        return static_cast<Motor*>(device_it->second)->GetStatusImpl(life_cycle_deduction);
    }
}

void EncosDriverManager::ConfigureAdapterStatusLifeCycle(BaseAdapter* adapter, int max_life_cycle) {
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    if (impl_->adapter_names.find(adapter) == impl_->adapter_names.end() ||
        impl_->deleting.count(adapter) != 0) {
        return;
    }
    impl_->adapter_status_configs[adapter].max_life_cycle = max_life_cycle;
    for (const auto& [key, value] : impl_->devices) {
        const auto bus_it = impl_->bus_keys.find(key.bus);
        if (key.kind == DeviceKind::Motor && bus_it != impl_->bus_keys.end() &&
            bus_it->second.adapter == adapter) {
            static_cast<Motor*>(value)->SetStatusLifeCycle(max_life_cycle);
        }
    }
}

int EncosDriverManager::GetAdapterStatusLifeCycle(BaseAdapter* adapter) const {
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    const auto found = impl_->adapter_status_configs.find(adapter);
    return found == impl_->adapter_status_configs.end() ? std::numeric_limits<int>::max()
                                                        : found->second.max_life_cycle;
}

void EncosDriverManager::ConfigureAdapterStatusMedianFilter(BaseAdapter* adapter,
                                                            std::size_t window_size) {
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    if (impl_->adapter_names.find(adapter) == impl_->adapter_names.end() ||
        impl_->deleting.count(adapter) != 0) {
        return;
    }
    impl_->adapter_status_configs[adapter].median_window_size = window_size;
    for (const auto& [key, value] : impl_->devices) {
        const auto bus_it = impl_->bus_keys.find(key.bus);
        if (key.kind == DeviceKind::Motor && bus_it != impl_->bus_keys.end() &&
            bus_it->second.adapter == adapter) {
            static_cast<Motor*>(value)->SetStatusMedianFilterWindowSizeImpl(window_size);
        }
    }
}

void EncosDriverManager::ConfigureAdapterStatusLimitFilter(BaseAdapter* adapter,
                                                           const MotorStatus& max_deltas) {
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    if (impl_->adapter_names.find(adapter) == impl_->adapter_names.end() ||
        impl_->deleting.count(adapter) != 0) {
        return;
    }
    impl_->adapter_status_configs[adapter].limit_max_deltas = max_deltas;
    for (const auto& [key, value] : impl_->devices) {
        const auto bus_it = impl_->bus_keys.find(key.bus);
        if (key.kind == DeviceKind::Motor && bus_it != impl_->bus_keys.end() &&
            bus_it->second.adapter == adapter) {
            static_cast<Motor*>(value)->SetStatusLimitFilterMaxDeltasImpl(max_deltas);
        }
    }
}

void EncosDriverManager::ConfigureAdapterStatusCallback(
    BaseAdapter* adapter, int raw_bus_idx, int motor_idx,
    std::function<void(const MotorStatus&)> callback) {
    const driver_manager_internal::RouteKey key{
        adapter, MakeReceiveUniqueId(raw_bus_idx, static_cast<std::uint32_t>(motor_idx))};
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    if (impl_->adapter_names.find(adapter) == impl_->adapter_names.end() ||
        impl_->deleting.count(adapter) != 0) {
        return;
    }
    if (callback) {
        impl_->status_callbacks.insert_or_assign(key, callback);
    } else {
        impl_->status_callbacks.erase(key);
    }
    const auto bus_it = impl_->buses.find({adapter, raw_bus_idx});
    if (bus_it != impl_->buses.end()) {
        const auto device_it = impl_->devices.find({bus_it->second, DeviceKind::Motor, motor_idx});
        if (device_it != impl_->devices.end()) {
            static_cast<Motor*>(device_it->second)->SetOnStatusImpl(std::move(callback));
        }
    }
}

void EncosDriverManager::ApplyMotorStatusConfigurationLocked(Bus* bus, int motor_idx,
                                                             Motor* motor) {
    Impl::StatusConfig config;
    std::function<void(const MotorStatus&)> callback;
    const auto bus_it = impl_->bus_keys.find(bus);
    if (bus_it == impl_->bus_keys.end() ||
        impl_->adapter_names.find(bus_it->second.adapter) == impl_->adapter_names.end() ||
        impl_->deleting.count(bus_it->second.adapter) != 0) {
        return;
    }
    const auto config_it = impl_->adapter_status_configs.find(bus_it->second.adapter);
    if (config_it != impl_->adapter_status_configs.end()) {
        config = config_it->second;
    }
    const driver_manager_internal::RouteKey key{
        bus_it->second.adapter,
        MakeReceiveUniqueId(bus_it->second.raw_idx, static_cast<std::uint32_t>(motor_idx))};
    const auto callback_it = impl_->status_callbacks.find(key);
    if (callback_it != impl_->status_callbacks.end()) {
        callback = callback_it->second;
    }
    motor->SetStatusLifeCycle(config.max_life_cycle);
    motor->SetStatusMedianFilterWindowSizeImpl(config.median_window_size);
    motor->SetStatusLimitFilterMaxDeltasImpl(config.limit_max_deltas);
    motor->SetOnStatusImpl(std::move(callback));
}

std::unordered_map<int, Bus*> EncosDriverManager::GetBuses(BaseAdapter* adapter) const {
    std::unordered_map<int, Bus*> result;
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    if (impl_->adapter_names.find(adapter) == impl_->adapter_names.end() ||
        impl_->deleting.count(adapter) != 0) {
        return result;
    }
    for (const auto& [key, value] : impl_->buses) {
        if (key.adapter == adapter && impl_->deleting.count(value) == 0 &&
            impl_->pending_buses.count(key) == 0) {
            result.emplace(key.raw_idx, value);
        }
    }
    return result;
}

}  // namespace encos
