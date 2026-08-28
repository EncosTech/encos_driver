#include "driver_manager_impl.h"
#include "utils/scope_exit.h"

namespace encos {

Motor* EncosDriverManager::ReconcileDiscoveredMotor(Bus* bus, int motor_idx, uint8_t frame_flags) {
    return ReconcileDiscoveredMotor(bus, motor_idx, frame_flags,
                                    CanFrameFlagsUseCanFd(frame_flags));
}

Motor* EncosDriverManager::ReconcileDiscoveredMotor(Bus* bus, int motor_idx, uint8_t frame_flags,
                                                    bool canfd) {
    const auto effective_frame_flags = driver_manager_internal::WithCanFdFlag(frame_flags, canfd);
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        const auto bus_it = impl_->bus_keys.find(bus);
        if (bus_it == impl_->bus_keys.end() || impl_->deleting.count(bus) != 0 ||
            impl_->deleting.count(bus_it->second.adapter) != 0 ||
            impl_->resetting_buses.count(bus) != 0) {
            return nullptr;
        }
        const auto device_it =
            impl_->devices.find({bus, driver_manager_internal::DeviceKind::Motor, motor_idx});
        if (device_it != impl_->devices.end()) {
            auto* motor = static_cast<Motor*>(device_it->second);
            if (impl_->deleting.count(motor) != 0) {
                return nullptr;
            }
            motor->impl_->frame_flags.store(effective_frame_flags, std::memory_order_relaxed);
            return motor;
        }
    }
    try {
        return CreateMotor(bus, motor_idx, effective_frame_flags, canfd);
    } catch (...) {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        const auto bus_it = impl_->bus_keys.find(bus);
        if (bus_it == impl_->bus_keys.end() || impl_->deleting.count(bus) != 0 ||
            impl_->resetting_buses.count(bus) != 0 ||
            impl_->deleting.count(bus_it->second.adapter) != 0) {
            return nullptr;
        }
        throw;
    }
}

bool EncosDriverManager::ResetBusMotorsToIdOne(Bus* bus) {
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        const auto bus_it = impl_->bus_keys.find(bus);
        if (bus_it == impl_->bus_keys.end() || impl_->deleting.count(bus) != 0 ||
            impl_->deleting.count(bus_it->second.adapter) != 0 ||
            !impl_->resetting_buses.insert(bus).second) {
            return false;
        }
    }
    const auto clear_reset = utils::MakeScopeExit([this, bus] {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        impl_->resetting_buses.erase(bus);
        impl_->deletion_condition.notify_all();
    });

    impl_->WaitForChildCreations(bus);
    for (;;) {
        std::vector<Motor*> motors;
        bool has_deleting_motor = false;
        {
            platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
            const auto bus_it = impl_->bus_keys.find(bus);
            if (bus_it == impl_->bus_keys.end() || impl_->deleting.count(bus) != 0 ||
                impl_->deleting.count(bus_it->second.adapter) != 0) {
                return false;
            }
            for (const auto& [key, device] : impl_->devices) {
                if (key.bus == bus && key.kind == driver_manager_internal::DeviceKind::Motor) {
                    motors.push_back(static_cast<Motor*>(device));
                }
            }
            has_deleting_motor =
                std::any_of(impl_->deleting_device_parents.begin(),
                            impl_->deleting_device_parents.end(), [bus](const auto& entry) {
                                return entry.second == bus;
                            });
        }
        if (has_deleting_motor) {
            impl_->WaitForBusDeviceDeletions(bus);
            continue;
        }
        if (motors.empty()) {
            return true;
        }
        if (motors.size() == 1) {
            platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
            const auto key = impl_->device_keys.find(motors.front());
            if (key != impl_->device_keys.end() && key->second.idx == 1 &&
                impl_->deleting.count(motors.front()) == 0) {
                return true;
            }
        }

        Motor* survivor = nullptr;
        for (auto* motor : motors) {
            platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
            const auto key = impl_->device_keys.find(motor);
            if (key != impl_->device_keys.end() && key->second.idx == 1) {
                survivor = motor;
                break;
            }
        }
        if (survivor == nullptr) {
            survivor = motors.front();
        }
        for (auto* motor : motors) {
            if (motor != survivor) {
                (void) DestroyMotor(motor);
            }
        }

        bool survivor_is_live = false;
        int survivor_idx = 0;
        {
            platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
            const auto key = impl_->device_keys.find(survivor);
            if (key != impl_->device_keys.end() &&
                key->second.kind == driver_manager_internal::DeviceKind::Motor &&
                key->second.bus == bus && impl_->deleting.count(survivor) == 0) {
                survivor_is_live = true;
                survivor_idx = key->second.idx;
            }
        }
        if (!survivor_is_live) {
            impl_->WaitForBusDeviceDeletions(bus);
            continue;
        }
        if (survivor_idx != 1 && !MigrateMotorIndex(survivor, 1)) {
            return false;
        }
    }
}
bool EncosDriverManager::HasRegisteredExternalDevice(Bus* bus) const {
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    const auto parent = impl_->bus_keys.find(bus);
    if (parent == impl_->bus_keys.end() || impl_->deleting.count(bus) != 0 ||
        impl_->deleting.count(parent->second.adapter) != 0) {
        return false;
    }
    return std::any_of(impl_->devices.begin(), impl_->devices.end(),
                       [this, bus](const auto& entry) {
                           return entry.first.bus == bus &&
                                  entry.first.kind != driver_manager_internal::DeviceKind::Motor &&
                                  impl_->deleting.count(entry.second) == 0;
                       });
}

bool EncosDriverManager::IsBusRegistered(BaseAdapter* adapter, int raw_bus_idx) const {
    auto& domain = adapter->impl_->route_domain;
    platform::LockGuard<platform::Mutex> lock(domain.mutex);
    return domain.buses.find(raw_bus_idx) != domain.buses.end();
}

bool EncosDriverManager::IsBusAlive(Bus* bus) const {
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    return impl_->bus_keys.find(bus) != impl_->bus_keys.end();
}

bool EncosDriverManager::RegisterReceiveRoutes(void* device, BaseAdapter* adapter, Bus* bus,
                                               const std::vector<std::uint32_t>& can_ids,
                                               ReceiveCallback callback,
                                               CancellationCallback cancel_waiters) {
    if (device == nullptr || adapter == nullptr || bus == nullptr || can_ids.empty()) {
        return false;
    }
    std::scoped_lock lock(impl_->object_mutex, impl_->route_mutex);
    const auto device_it = impl_->device_keys.find(device);
    const auto bus_it = impl_->bus_keys.find(bus);
    if (device_it == impl_->device_keys.end() || bus_it == impl_->bus_keys.end() ||
        device_it->second.bus != bus || bus_it->second.adapter != adapter ||
        impl_->deleting.count(device) != 0 || impl_->deleting.count(bus) != 0 ||
        impl_->deleting.count(adapter) != 0) {
        return false;
    }
    const auto existing = impl_->device_routes.find(device);
    if (existing != impl_->device_routes.end()) {
        if (existing->second.size() != can_ids.size()) {
            return false;
        }
        std::vector<ReceiveCallback> prepared_callbacks;
        prepared_callbacks.reserve(existing->second.size());
        for (const auto& key : existing->second) {
            if (key.adapter != adapter ||
                std::find(can_ids.begin(), can_ids.end(),
                          static_cast<std::uint32_t>(key.unique_id & 0xFFFFFFFFu)) ==
                    can_ids.end()) {
                return false;
            }
            const auto route = impl_->routes.find(key);
            if (route == impl_->routes.end() || route->second->device != device) {
                return false;
            }
            platform::LockGuard<platform::Mutex> route_lock(route->second->mutex);
            if (route->second->retiring || route->second->callback) {
                return false;
            }
            prepared_callbacks.push_back(callback);
        }
        CancellationCallback prepared_cancel = std::move(cancel_waiters);
        for (std::size_t index = 0; index < existing->second.size(); ++index) {
            const auto& route = impl_->routes.at(existing->second[index]);
            platform::LockGuard<platform::Mutex> route_lock(route->mutex);
            route->callback = std::move(prepared_callbacks[index]);
        }
        impl_->cancellations.insert_or_assign(device, std::move(prepared_cancel));
        return true;
    }
    return impl_->PublishRoutesLocked(device, adapter, bus, can_ids, std::move(callback),
                                      std::move(cancel_waiters));
}

bool EncosDriverManager::DispatchReceive(BaseAdapter* adapter, int raw_bus_idx,
                                         const MotorPackMsg& message) {
    auto& domain = adapter->impl_->route_domain;
    if (message.id == 0x7FFu) {
        std::vector<std::shared_ptr<RouteRecord>> system_routes;
        {
            platform::LockGuard<platform::Mutex> lock(domain.mutex);
            system_routes.reserve(domain.routes.size());
            for (const auto& [unique_id, route] : domain.routes) {
                if (static_cast<std::uint32_t>(unique_id >> 32u) !=
                    static_cast<std::uint32_t>(raw_bus_idx)) {
                    continue;
                }
                platform::LockGuard<platform::Mutex> route_lock(route->mutex);
                if (route->retiring || !route->callback) {
                    continue;
                }
                ++route->in_flight;
                system_routes.push_back(route);
            }
        }
        for (const auto& route : system_routes) {
            const auto previous = driver_manager_internal::callback_context;
            driver_manager_internal::callback_context =
                driver_manager_internal::CallbackContext{route->device, route->bus, route->adapter};
            try {
                route->callback(message);
            } catch (...) {}
            driver_manager_internal::callback_context = previous;
            {
                platform::LockGuard<platform::Mutex> lock(route->mutex);
                --route->in_flight;
                if (route->in_flight == 0) {
                    route->condition.notify_all();
                }
            }
        }
        // 共享系统响应仍进入未知邮箱，供总线级协议消费者读取。
        return false;
    }

    std::shared_ptr<RouteRecord> route;
    {
        platform::LockGuard<platform::Mutex> lock(domain.mutex);
        const auto found = domain.routes.find(
            MakeReceiveUniqueId(raw_bus_idx, static_cast<std::uint32_t>(message.id)));
        if (found == domain.routes.end()) {
            return false;
        }
        route = found->second;
        platform::LockGuard<platform::Mutex> route_lock(route->mutex);
        if (route->retiring || !route->callback) {
            return false;
        }
        ++route->in_flight;
    }

    const auto previous = driver_manager_internal::callback_context;
    driver_manager_internal::callback_context =
        driver_manager_internal::CallbackContext{route->device, route->bus, route->adapter};
    try {
        if (route->callback) {
            route->callback(message);
        }
    } catch (...) {}
    driver_manager_internal::callback_context = previous;

    {
        platform::LockGuard<platform::Mutex> lock(route->mutex);
        --route->in_flight;
        if (route->in_flight == 0) {
            route->condition.notify_all();
        }
    }
    return true;
}

bool EncosDriverManager::DispatchUnknownReceive(BaseAdapter* adapter, int raw_bus_idx,
                                                const MotorPackMsg& message) noexcept {
    auto& domain = adapter->impl_->route_domain;
    platform::LockGuard<platform::Mutex> lock(domain.mutex);
    const auto found = domain.buses.find(raw_bus_idx);
    if (found == domain.buses.end()) {
        return false;
    }
    found->second->impl_->unknown_messages.Push(message);
    return true;
}

void EncosDriverManager::DispatchRawReceiveCallback(
    BaseAdapter* adapter, int raw_bus_idx,
    const std::function<void(const MotorMessages&)>& callback,
    const MotorMessages& messages) noexcept {
    (void) raw_bus_idx;
    const auto previous = driver_manager_internal::callback_context;
    driver_manager_internal::callback_context =
        driver_manager_internal::CallbackContext{nullptr, nullptr, adapter, true};
    try {
        callback(messages);
    } catch (...) {}
    driver_manager_internal::callback_context = previous;
}
bool EncosDriverManager::ReserveMotorIndex(Motor* motor, int new_motor_idx) {
    if (motor == nullptr) {
        return false;
    }
    MigrationHook migration_hook;
    bool newly_reserved = false;
    {
        std::scoped_lock lock(impl_->object_mutex, impl_->route_mutex);
        const auto reverse = impl_->device_keys.find(motor);
        if (reverse == impl_->device_keys.end() ||
            reverse->second.kind != driver_manager_internal::DeviceKind::Motor ||
            impl_->deleting.count(motor) != 0) {
            return false;
        }
        const driver_manager_internal::DeviceKey new_key{
            reverse->second.bus, driver_manager_internal::DeviceKind::Motor, new_motor_idx};
        if (new_key == reverse->second) {
            return true;
        }
        const auto bus_it = impl_->bus_keys.find(reverse->second.bus);
        if (bus_it == impl_->bus_keys.end() || impl_->deleting.count(reverse->second.bus) != 0 ||
            impl_->deleting.count(bus_it->second.adapter) != 0 ||
            impl_->devices.find(new_key) != impl_->devices.end() ||
            impl_->pending_devices.find(new_key) != impl_->pending_devices.end()) {
            return false;
        }
        const driver_manager_internal::RouteKey new_route{
            bus_it->second.adapter,
            MakeReceiveUniqueId(bus_it->second.raw_idx, static_cast<std::uint32_t>(new_motor_idx))};
        if (impl_->routes.find(new_route) != impl_->routes.end()) {
            return false;
        }
        const auto device_reservation = impl_->motor_index_reservations.find(new_key);
        if (device_reservation != impl_->motor_index_reservations.end()) {
            return device_reservation->second.motor == motor;
        }
        const auto route_reservation = impl_->motor_route_reservations.find(new_route);
        if (route_reservation != impl_->motor_route_reservations.end()) {
            return route_reservation->second == motor;
        }
        std::function<void(const MotorStatus&)> prepared_callback;
        bool install_callback = false;
        try {
            migration_hook = impl_->migration_hook;
            const auto motor_routes = impl_->device_routes.find(motor);
            if (motor_routes == impl_->device_routes.end() || motor_routes->second.size() != 1) {
                return false;
            }
            const auto target_callback = impl_->status_callbacks.find(new_route);
            const auto source_callback = impl_->status_callbacks.find(motor_routes->second.front());
            if (target_callback != impl_->status_callbacks.end()) {
                prepared_callback = target_callback->second;
                install_callback = true;
            } else if (source_callback != impl_->status_callbacks.end()) {
                prepared_callback = source_callback->second;
                install_callback = true;
            }
            impl_->devices.reserve(impl_->devices.size() + 1);
            impl_->routes.reserve(impl_->routes.size() + 1);
            impl_->status_callbacks.reserve(impl_->status_callbacks.size() + 1);
            impl_->motor_index_reservations.emplace(
                new_key,
                Impl::MotorIndexReservation{motor, std::move(prepared_callback), install_callback});
            try {
                impl_->motor_route_reservations.emplace(new_route, motor);
            } catch (...) {
                impl_->motor_index_reservations.erase(new_key);
                throw;
            }
            newly_reserved = true;
        } catch (...) {
            return false;
        }
    }
    if (newly_reserved && migration_hook) {
        try {
            migration_hook(MigrationStage::BeforeCurrentRangeMove);
        } catch (...) {
            ReleaseMotorIndexReservation(motor, new_motor_idx);
            return false;
        }
    }
    return true;
}

void EncosDriverManager::ReleaseMotorIndexReservation(Motor* motor, int new_motor_idx) noexcept {
    std::scoped_lock lock(impl_->object_mutex, impl_->route_mutex);
    const auto reverse = impl_->device_keys.find(motor);
    if (reverse == impl_->device_keys.end()) {
        return;
    }
    const driver_manager_internal::DeviceKey new_key{
        reverse->second.bus, driver_manager_internal::DeviceKind::Motor, new_motor_idx};
    const auto device_reservation = impl_->motor_index_reservations.find(new_key);
    if (device_reservation == impl_->motor_index_reservations.end() ||
        device_reservation->second.motor != motor) {
        return;
    }
    const auto bus_it = impl_->bus_keys.find(reverse->second.bus);
    if (bus_it != impl_->bus_keys.end()) {
        const driver_manager_internal::RouteKey new_route{
            bus_it->second.adapter,
            MakeReceiveUniqueId(bus_it->second.raw_idx, static_cast<std::uint32_t>(new_motor_idx))};
        const auto route_reservation = impl_->motor_route_reservations.find(new_route);
        if (route_reservation != impl_->motor_route_reservations.end() &&
            route_reservation->second == motor) {
            impl_->motor_route_reservations.erase(route_reservation);
        }
    }
    impl_->motor_index_reservations.erase(device_reservation);
    impl_->deletion_condition.notify_all();
}

bool EncosDriverManager::MigrateMotorIndex(Motor* motor, int new_motor_idx) {
    if (motor == nullptr) {
        return false;
    }
    MigrationHook migration_hook;
    bool migration_prechecked = false;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        try {
            migration_hook = impl_->migration_hook;
        } catch (...) {
            return false;
        }
        const auto reverse = impl_->device_keys.find(motor);
        if (reverse != impl_->device_keys.end()) {
            const driver_manager_internal::DeviceKey new_key{
                reverse->second.bus, driver_manager_internal::DeviceKind::Motor, new_motor_idx};
            const auto reservation = impl_->motor_index_reservations.find(new_key);
            migration_prechecked = reservation != impl_->motor_index_reservations.end() &&
                                   reservation->second.motor == motor;
        }
    }
    if (!migration_prechecked && migration_hook) {
        try {
            migration_hook(MigrationStage::BeforeCurrentRangeMove);
        } catch (...) {
            return false;
        }
    }
    std::scoped_lock lock(impl_->object_mutex, impl_->route_mutex);
    const auto reverse = impl_->device_keys.find(motor);
    if (reverse == impl_->device_keys.end() ||
        reverse->second.kind != driver_manager_internal::DeviceKind::Motor ||
        impl_->deleting.count(motor) != 0) {
        return false;
    }
    const driver_manager_internal::DeviceKey old_key = reverse->second;
    const driver_manager_internal::DeviceKey new_key{
        old_key.bus, driver_manager_internal::DeviceKind::Motor, new_motor_idx};
    if (old_key == new_key) {
        return true;
    }
    const auto device_reservation = impl_->motor_index_reservations.find(new_key);
    if (impl_->devices.find(new_key) != impl_->devices.end() ||
        (device_reservation != impl_->motor_index_reservations.end() &&
         device_reservation->second.motor != motor)) {
        return false;
    }
    const auto bus_it = impl_->bus_keys.find(old_key.bus);
    if (bus_it == impl_->bus_keys.end() || impl_->deleting.count(old_key.bus) != 0 ||
        impl_->deleting.count(bus_it->second.adapter) != 0) {
        return false;
    }
    const auto bus_key = bus_it->second;
    auto& domain = bus_key.adapter->impl_->route_domain;
    platform::LockGuard<platform::Mutex> domain_lock(domain.mutex);
    const driver_manager_internal::RouteKey new_route{
        bus_key.adapter,
        MakeReceiveUniqueId(bus_key.raw_idx, static_cast<std::uint32_t>(new_motor_idx))};
    const auto route_reservation = impl_->motor_route_reservations.find(new_route);
    if (impl_->routes.find(new_route) != impl_->routes.end() ||
        (route_reservation != impl_->motor_route_reservations.end() &&
         route_reservation->second != motor)) {
        return false;
    }
    const auto routes_it = impl_->device_routes.find(motor);
    if (routes_it == impl_->device_routes.end() || routes_it->second.size() != 1) {
        return false;
    }
    const auto old_route = routes_it->second.front();
    const auto old_route_it = impl_->routes.find(old_route);
    if (old_route_it == impl_->routes.end()) {
        return false;
    }
    const auto record = old_route_it->second;
    const auto source_callback = impl_->status_callbacks.find(old_route);
    const auto target_callback = impl_->status_callbacks.find(new_route);
    std::function<void(const MotorStatus&)> callback_to_install;
    bool install_callback = false;
    if (device_reservation != impl_->motor_index_reservations.end() &&
        device_reservation->second.motor == motor) {
        callback_to_install = std::move(device_reservation->second.callback);
        install_callback = device_reservation->second.install_callback;
    } else {
        try {
            if (target_callback != impl_->status_callbacks.end()) {
                callback_to_install = target_callback->second;
                install_callback = true;
            } else if (source_callback != impl_->status_callbacks.end()) {
                callback_to_install = source_callback->second;
                install_callback = true;
            }
        } catch (...) {
            return false;
        }
    }

    try {
        impl_->routes.reserve(impl_->routes.size() + 1);
        domain.routes.reserve(domain.routes.size() + 1);
        impl_->devices.reserve(impl_->devices.size() + 1);
        impl_->status_callbacks.reserve(impl_->status_callbacks.size() + 1);
        routes_it->second.reserve(1);
    } catch (...) {
        return false;
    }
    try {
        if (!impl_->routes.emplace(new_route, record).second) {
            return false;
        }
        try {
            if (!domain.routes.emplace(new_route.unique_id, record).second) {
                impl_->routes.erase(new_route);
                return false;
            }
            if (!impl_->devices.emplace(new_key, motor).second) {
                domain.routes.erase(new_route.unique_id);
                impl_->routes.erase(new_route);
                return false;
            }
        } catch (...) {
            domain.routes.erase(new_route.unique_id);
            impl_->routes.erase(new_route);
            return false;
        }
    } catch (...) {
        return false;
    }

    impl_->routes.erase(old_route);
    domain.routes.erase(old_route.unique_id);
    impl_->devices.erase(old_key);
    routes_it->second.front() = new_route;
    reverse->second = new_key;
    if (target_callback != impl_->status_callbacks.end()) {
        impl_->status_callbacks.erase(old_route);
    } else {
        auto callback_node = impl_->status_callbacks.extract(old_route);
        if (!callback_node.empty()) {
            callback_node.key() = new_route;
            impl_->status_callbacks.insert(std::move(callback_node));
        }
    }
    if (install_callback) {
        motor->SetOnStatusImpl(std::move(callback_to_install));
    }
    motor->impl_->idx.store(static_cast<uint16_t>(new_motor_idx), std::memory_order_relaxed);
    if (device_reservation != impl_->motor_index_reservations.end()) {
        impl_->motor_index_reservations.erase(device_reservation);
    }
    if (route_reservation != impl_->motor_route_reservations.end()) {
        impl_->motor_route_reservations.erase(route_reservation);
    }
    impl_->deletion_condition.notify_all();
    return true;
}

}  // namespace encos
