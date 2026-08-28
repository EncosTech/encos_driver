#include "driver_manager_impl.h"

namespace encos {
namespace driver_manager_internal {

template <typename T>
bool DestroyDevice(EncosDriverManager::Impl* impl, T* device, DeviceKind expected) {
    if (device == nullptr) {
        return false;
    }
    DeviceKey key;
    OperationGate* operation_gate = nullptr;
    {
        std::scoped_lock lock(impl->object_mutex, impl->route_mutex);
        const auto found = impl->device_keys.find(device);
        if (found == impl->device_keys.end() || expected != found->second.kind ||
            impl->deleting.count(device) != 0) {
            return false;
        }
        key = found->second;
        if (callback_context.device == device) {
            return false;
        }
        if (callback_context.device != nullptr) {
            for (const auto& [route_key, route] : impl->routes) {
                (void) route_key;
                if (route->device != device) {
                    continue;
                }
                platform::LockGuard<platform::Mutex> route_lock(route->mutex);
                if (route->in_flight != 0) {
                    return false;
                }
            }
        }
        impl->deleting.insert(device);
        impl->deleting_device_parents.emplace(device, key.bus);
        operation_gate = impl->operation_registry.Retire(device, ToOperationKind(expected));
        if (operation_gate == nullptr) {
            std::terminate();
        }
    }

    EncosDriverManager::CancellationCallback cancel;
    auto retired = impl->RetireRoutes(device, cancel);
    if (cancel) {
        try {
            cancel();
        } catch (...) {}
    }
    for (const auto& route : retired) {
        platform::UniqueLock<platform::Mutex> lock(route->mutex);
        if (route->in_flight != 0) {
            impl->ObserveWaitForTests();
        }
        route->condition.wait(lock, [&route]() {
            return route->in_flight == 0;
        });
    }

    if (operation_gate->HasActiveOperations()) {
        impl->ObserveWaitForTests();
    }
    operation_gate->WaitForDrain();
    impl->operation_registry.ReclaimRetired(device, ToOperationKind(expected));

    impl->InvokeDeletionHook(EncosDriverManager::DeletionStage::BeforeDeviceDestroy);

    {
        platform::LockGuard<platform::Mutex> lock(impl->object_mutex);
        impl->devices.erase(key);
        impl->device_keys.erase(device);
    }
    delete device;
    {
        platform::LockGuard<platform::Mutex> lock(impl->object_mutex);
        impl->deleting.erase(device);
        impl->deleting_device_parents.erase(device);
        impl->deletion_condition.notify_all();
    }
    return true;
}

}  // namespace driver_manager_internal

void EncosDriverManager::DestroyAllManagedObjects() {
    for (;;) {
        BaseAdapter* adapter = nullptr;
        {
            platform::UniqueLock<platform::Mutex> lock(impl_->object_mutex);
            if (impl_->adapters.empty()) {
                break;
            }
            adapter = impl_->adapters.begin()->second;
            if (impl_->deleting.count(adapter) != 0) {
                impl_->deletion_condition.wait(lock, [this, adapter]() {
                    return impl_->adapter_names.find(adapter) == impl_->adapter_names.end() ||
                           impl_->deleting.count(adapter) == 0;
                });
                continue;
            }
        }
        if (!DestroyAdapter(adapter)) {
            throw std::runtime_error(
                "DestroyAllManagedObjects cannot destroy an adapter in this context");
        }
    }
}

bool EncosDriverManager::DestroyMotor(Motor* motor) {
    using driver_manager_internal::DestroyDevice;
    using driver_manager_internal::DeviceKind;

    return DestroyDevice(impl_, motor, DeviceKind::Motor);
}

bool EncosDriverManager::DestroyBattery(Battery* battery) {
    using driver_manager_internal::DestroyDevice;
    using driver_manager_internal::DeviceKind;

    return DestroyDevice(impl_, battery, DeviceKind::Battery);
}

bool EncosDriverManager::DestroyImu(Imu* imu) {
    using driver_manager_internal::DestroyDevice;
    using driver_manager_internal::DeviceKind;

    return DestroyDevice(impl_, imu, DeviceKind::Imu);
}

bool EncosDriverManager::DestroyPms(Pms* pms) {
    using driver_manager_internal::DestroyDevice;
    using driver_manager_internal::DeviceKind;

    return DestroyDevice(impl_, pms, DeviceKind::Pms);
}

bool EncosDriverManager::DestroyGlove(Glove* glove) {
    using driver_manager_internal::callback_context;
    using driver_manager_internal::GloveKey;

    if (glove == nullptr) {
        return false;
    }
    GloveKey key;
    OperationGate* operation_gate = nullptr;
    {
        std::scoped_lock lock(impl_->object_mutex, impl_->route_mutex);
        const auto found = impl_->glove_keys.find(glove);
        if (found == impl_->glove_keys.end() || impl_->deleting_gloves.count(glove) != 0) {
            return false;
        }
        if (callback_context.raw_receive) {
            // 原始接收回调上下文中销毁必然被 DestroyBus 拒绝，直接拒绝避免无谓流程。
            return false;
        }
        if (callback_context.device != nullptr) {
            // 回调上下文内销毁必须保证手套全部路由（55 条：50 编码器 + 5 校准）
            // 均无在飞回调：否则销毁总线被拒后 facade 仍会被删除，当前回调栈悬空。
            for (const auto& [route_key, route] : impl_->routes) {
                (void) route_key;
                const bool belongs_to_glove =
                    std::find(glove->impl_->buses.begin(), glove->impl_->buses.end(), route->bus) !=
                    glove->impl_->buses.end();
                if (!belongs_to_glove) {
                    continue;
                }
                platform::LockGuard<platform::Mutex> route_lock(route->mutex);
                if (route->in_flight != 0) {
                    return false;
                }
            }
        }
        key = found->second;
        impl_->deleting_gloves.insert(glove);
        operation_gate = impl_->operation_registry.Retire(glove, OperationKind::Glove);
        if (operation_gate == nullptr) {
            std::terminate();
        }
    }

    // 整手校准持有 facade 的 operation gate；先等待它自然完成，再销毁并排空
    // 5 条帧内总线（手指 1-4 先于手指 0），最后释放 facade。
    if (operation_gate->HasActiveOperations()) {
        impl_->ObserveWaitForTests();
    }
    operation_gate->WaitForDrain();
    impl_->operation_registry.ReclaimRetired(glove, OperationKind::Glove);
    bool all_destroyed = true;
    for (std::size_t finger = 1; finger < 5; ++finger) {
        Bus* finger_bus = glove->impl_->buses[finger];
        if (finger_bus == nullptr) {
            continue;
        }
        if (!DestroyGloveInternalBus(finger_bus)) {
            // 总线正被并发销毁（违约路径）：等待其设备全部销毁并回填空槽，
            // 避免后续删除 facade 时仍有存活设备引用它。
            impl_->WaitForBusChildren(finger_bus);
            all_destroyed = false;
        }
    }
    Bus* finger_zero_bus = glove->impl_->buses[0];
    if (finger_zero_bus != nullptr) {
        if (!DestroyGloveInternalBus(finger_zero_bus)) {
            impl_->WaitForBusChildren(finger_zero_bus);
            all_destroyed = false;
        }
    }
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        impl_->gloves.erase(key);
        impl_->glove_keys.erase(glove);
    }
    delete glove;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        impl_->deleting_gloves.erase(glove);
        impl_->deletion_condition.notify_all();
    }
    return all_destroyed;
}

bool EncosDriverManager::DestroyBus(Bus* bus) {
    return DestroyBusImpl(bus, false);
}

bool EncosDriverManager::DestroyGloveInternalBus(Bus* bus) {
    return DestroyBusImpl(bus, true);
}

bool EncosDriverManager::DestroyBusImpl(Bus* bus, bool allow_glove_internal) {
    using driver_manager_internal::BusKey;
    using driver_manager_internal::callback_context;
    using driver_manager_internal::DeviceKind;
    using driver_manager_internal::ToOperationKind;

    if (bus == nullptr) {
        return false;
    }
    BusKey key;
    OperationGate* operation_gate = nullptr;
    std::vector<std::pair<void*, DeviceKind>> children;
    {
        std::scoped_lock lock(impl_->object_mutex, impl_->route_mutex);
        const auto found = impl_->bus_keys.find(bus);
        if (found == impl_->bus_keys.end() || impl_->deleting.count(bus) != 0 ||
            callback_context.bus == bus ||
            (callback_context.raw_receive && callback_context.adapter == found->second.adapter)) {
            return false;
        }
        if (!allow_glove_internal && (impl_->glove_internal_buses.count(bus) != 0 ||
                                      impl_->pending_glove_bus_keys.count(found->second) != 0)) {
            return false;
        }
        if (callback_context.device != nullptr) {
            for (const auto& [route_key, route] : impl_->routes) {
                (void) route_key;
                if (route->bus != bus) {
                    continue;
                }
                platform::LockGuard<platform::Mutex> route_lock(route->mutex);
                if (route->in_flight != 0) {
                    return false;
                }
            }
        }
        key = found->second;
        impl_->deleting.insert(bus);
        impl_->deleting_bus_parents.emplace(bus, key.adapter);
        {
            auto& domain = key.adapter->impl_->route_domain;
            platform::LockGuard<platform::Mutex> domain_lock(domain.mutex);
            domain.buses.erase(key.raw_idx);
        }
        operation_gate = impl_->operation_registry.Retire(bus, OperationKind::Bus);
        if (operation_gate == nullptr) {
            std::terminate();
        }
        for (const auto& [device_key, child] : impl_->devices) {
            if (device_key.bus != bus) {
                continue;
            }
            (void) impl_->operation_registry.Retire(child, ToOperationKind(device_key.kind));
        }
    }
    if (operation_gate->HasActiveOperations()) {
        impl_->ObserveWaitForTests();
    }
    operation_gate->WaitForDrain();
    impl_->operation_registry.ReclaimRetired(bus, OperationKind::Bus);
    impl_->WaitForChildCreations(bus);
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        for (const auto& [device_key, value] : impl_->devices) {
            if (device_key.bus == bus) {
                children.emplace_back(value, device_key.kind);
            }
        }
    }
    for (const auto& [child, kind] : children) {
        switch (kind) {
            case DeviceKind::Motor:
                (void) DestroyMotor(static_cast<Motor*>(child));
                break;
            case DeviceKind::Battery:
                (void) DestroyBattery(static_cast<Battery*>(child));
                break;
            case DeviceKind::Imu:
                (void) DestroyImu(static_cast<Imu*>(child));
                break;
            case DeviceKind::Pms:
                (void) DestroyPms(static_cast<Pms*>(child));
                break;
            case DeviceKind::GloveEncoder:
                (void) driver_manager_internal::DestroyDevice<GloveEncoder>(
                    impl_, static_cast<GloveEncoder*>(child), DeviceKind::GloveEncoder);
                break;
            case DeviceKind::GloveCalibrator:
                (void) driver_manager_internal::DestroyDevice<GloveCalibrator>(
                    impl_, static_cast<GloveCalibrator*>(child), DeviceKind::GloveCalibrator);
                break;
        }
    }
    impl_->WaitForBusChildren(bus);
    impl_->InvokeDeletionHook(DeletionStage::BeforeBusDestroy);
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        impl_->buses.erase(key);
        impl_->bus_keys.erase(bus);
        impl_->glove_internal_buses.erase(bus);
    }
    delete bus;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        impl_->deleting.erase(bus);
        impl_->deleting_bus_parents.erase(bus);
        impl_->deletion_condition.notify_all();
    }
    return true;
}

bool EncosDriverManager::DestroyAdapter(BaseAdapter* adapter) {
    using driver_manager_internal::callback_context;
    using driver_manager_internal::ToOperationKind;

    if (adapter == nullptr) {
        return false;
    }
    std::string name;
    OperationGate* operation_gate = nullptr;
    std::vector<Bus*> children;
    {
        std::scoped_lock lock(impl_->object_mutex, impl_->route_mutex);
        const auto found = impl_->adapter_names.find(adapter);
        if (found == impl_->adapter_names.end() || impl_->deleting.count(adapter) != 0 ||
            callback_context.adapter == adapter) {
            return false;
        }
        if (callback_context.adapter != nullptr &&
            impl_->operation_registry.HasActiveOperations(adapter, OperationKind::Adapter)) {
            return false;
        }
        if (callback_context.device != nullptr || callback_context.raw_receive) {
            for (const auto& [route_key, route] : impl_->routes) {
                (void) route_key;
                if (route->adapter != adapter) {
                    continue;
                }
                platform::LockGuard<platform::Mutex> route_lock(route->mutex);
                if (route->in_flight != 0) {
                    return false;
                }
            }
        }
        name = found->second;
        impl_->deleting.insert(adapter);
        for (const auto& [bus_key, bus] : impl_->buses) {
            if (bus_key.adapter != adapter) {
                continue;
            }
            if (impl_->glove_internal_buses.count(bus) != 0) {
                continue;
            }
            (void) impl_->operation_registry.Retire(bus, OperationKind::Bus);
            for (const auto& [device_key, child] : impl_->devices) {
                if (device_key.bus != bus) {
                    continue;
                }
                (void) impl_->operation_registry.Retire(child, ToOperationKind(device_key.kind));
            }
        }
        for (const auto& [glove_key, glove] : impl_->gloves) {
            if (glove_key.adapter == adapter) {
                (void) impl_->operation_registry.Retire(glove, OperationKind::Glove);
            }
        }
    }
    impl_->WaitForChildCreations(adapter);
    std::vector<Glove*> glove_children;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        for (const auto& [key, bus] : impl_->buses) {
            if (key.adapter == adapter) {
                children.push_back(bus);
            }
        }
        for (const auto& [key, glove] : impl_->gloves) {
            if (key.adapter == adapter) {
                glove_children.push_back(glove);
            }
        }
    }
    // 先销毁手套整手（其帧内总线与 55 个设备随之销毁），再销毁剩余总线，
    // 避免 facade 持有已销毁总线的悬空指针。
    for (auto* glove : glove_children) {
        (void) DestroyGlove(glove);
    }
    for (auto* bus : children) {
        (void) DestroyBus(bus);
    }
    impl_->WaitForAdapterBuses(adapter);
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        operation_gate = impl_->operation_registry.Retire(adapter, OperationKind::Adapter);
        if (operation_gate == nullptr) {
            std::terminate();
        }
    }
    impl_->InvokeDeletionHook(DeletionStage::BeforeAdapterReceiveDrain);
    if (operation_gate->HasActiveOperations()) {
        impl_->ObserveWaitForTests();
    }
    operation_gate->WaitForDrain();
    impl_->operation_registry.ReclaimRetired(adapter, OperationKind::Adapter);
    impl_->InvokeDeletionHook(DeletionStage::BeforeAdapterDestroy);
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        impl_->adapters.erase(name);
        impl_->adapter_names.erase(adapter);
        impl_->adapter_status_configs.erase(adapter);
        for (auto it = impl_->status_callbacks.begin(); it != impl_->status_callbacks.end();) {
            if (it->first.adapter == adapter) {
                it = impl_->status_callbacks.erase(it);
            } else {
                ++it;
            }
        }
    }
    delete adapter;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        impl_->deleting.erase(adapter);
        impl_->deletion_condition.notify_all();
    }
    return true;
}

bool EncosDriverManager::DestroyAdapterByInterfaceName(const std::string& interface_name) {
    BaseAdapter* adapter = nullptr;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        const auto found = impl_->adapters.find(interface_name);
        if (found == impl_->adapters.end()) {
            return false;
        }
        adapter = found->second;
    }
    return DestroyAdapter(adapter);
}

}  // namespace encos
