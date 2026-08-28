#include "driver_manager_impl.h"

namespace encos {
namespace driver_manager_internal {

struct DeviceRoute {
    EncosDriverManager::ReceiveCallback callback;
    EncosDriverManager::CancellationCallback cancel_waiters;
};

template <typename T, typename Factory, typename RouteFactory, typename Initializer,
          typename Publisher>
T* CreateDeviceWithRoutes(EncosDriverManager::Impl* impl, Bus* bus, DeviceKind kind, int idx,
                          Factory factory, RouteFactory route_factory, Initializer initializer,
                          Publisher publisher) {
    if (bus == nullptr) {
        throw std::invalid_argument("Bus is null");
    }
    const DeviceKey key{bus, kind, idx};
    std::shared_ptr<PendingCreation> publication;
    bool leader = false;
    BusKey bus_key;
    {
        platform::UniqueLock<platform::Mutex> lock(impl->object_mutex);
        for (;;) {
            const auto parent = impl->bus_keys.find(bus);
            if (parent == impl->bus_keys.end() || impl->deleting.count(bus) != 0 ||
                impl->resetting_buses.count(bus) != 0 ||
                impl->deleting.count(parent->second.adapter) != 0) {
                throw std::invalid_argument("Bus is not registered");
            }
            bus_key = parent->second;
            const auto found = impl->devices.find(key);
            if (found != impl->devices.end()) {
                auto* device = found->second;
                if (impl->deleting.count(device) == 0) {
                    return static_cast<T*>(device);
                }
                impl->ObserveWaitForTests();
                impl->deletion_condition.wait(lock, [impl, &key, device]() {
                    const auto current = impl->devices.find(key);
                    return current == impl->devices.end() || current->second != device ||
                           impl->deleting.count(device) == 0;
                });
                continue;
            }
            if (void* result = AwaitOrLead(lock, impl->pending_devices, key, publication, leader)) {
                if (impl->deleting.count(result) == 0) {
                    return static_cast<T*>(result);
                }
                continue;
            }
            if (leader) {
                impl->BeginChildCreationLocked(bus);
                impl->BeginChildCreationLocked(bus_key.adapter);
            }
            break;
        }
    }

    T* device = nullptr;
    OperationGate* operation_gate = nullptr;
    try {
        device = factory();
        {
            platform::LockGuard<platform::Mutex> lock(impl->object_mutex);
            operation_gate = impl->operation_registry.Register(device, ToOperationKind(kind));
        }
        const DeviceRoute route = route_factory(device);
        impl->InvokeCreationHook(EncosDriverManager::CreationStage::BeforeDevicePublish);
        {
            std::scoped_lock lock(impl->object_mutex, impl->route_mutex);
            if (impl->deleting.count(bus) != 0 || impl->resetting_buses.count(bus) != 0 ||
                impl->deleting.count(bus_key.adapter) != 0) {
                throw std::runtime_error("Device parent is retiring");
            }
            if (!impl->PublishRoutesLocked(device, bus_key.adapter, bus, impl->RouteIds(kind, idx),
                                           route.callback, route.cancel_waiters)) {
                throw std::runtime_error("Receive route already registered");
            }
            impl->initializing_device_keys.emplace(device, key);
        }
        impl->InvokeDeviceInitializerHook(device);
        initializer(device);
        impl->InvokeCreationHook(EncosDriverManager::CreationStage::BeforeDeviceCommit);
        {
            platform::LockGuard<platform::Mutex> lock(impl->object_mutex);
            if (impl->deleting.count(bus) != 0 || impl->resetting_buses.count(bus) != 0 ||
                impl->deleting.count(bus_key.adapter) != 0) {
                throw std::runtime_error("Device parent is retiring");
            }
            publisher(device);
            try {
                impl->initializing_device_keys.erase(device);
                impl->devices.emplace(key, device);
                impl->device_keys.emplace(device, key);
            } catch (...) {
                impl->devices.erase(key);
                impl->device_keys.erase(device);
                throw;
            }
            impl->pending_devices.erase(key);
            CompletePending(publication, device);
        }
        impl->EndChildCreation(bus);
        impl->EndChildCreation(bus_key.adapter);
        return device;
    } catch (...) {
        if (device != nullptr) {
            operation_gate = impl->operation_registry.Retire(device, ToOperationKind(kind));
            EncosDriverManager::CancellationCallback cancel;
            auto retired = impl->RetireRoutes(device, cancel);
            if (cancel) {
                try {
                    cancel();
                } catch (...) {}
            }
            for (const auto& route : retired) {
                platform::UniqueLock<platform::Mutex> lock(route->mutex);
                route->condition.wait(lock, [&route]() {
                    return route->in_flight == 0;
                });
            }
            if (operation_gate != nullptr) {
                operation_gate->WaitForDrain();
                impl->operation_registry.ReclaimRetired(device, ToOperationKind(kind));
            }
        }
        {
            platform::LockGuard<platform::Mutex> lock(impl->object_mutex);
            impl->devices.erase(key);
            impl->device_keys.erase(device);
            impl->initializing_device_keys.erase(device);
            impl->pending_devices.erase(key);
            CompletePending(publication, nullptr, std::current_exception());
        }
        delete device;
        impl->EndChildCreation(bus);
        impl->EndChildCreation(bus_key.adapter);
        throw;
    }
}

}  // namespace driver_manager_internal

Bus* EncosDriverManager::CreateBus(BaseAdapter* adapter, int raw_bus_idx) {
    using driver_manager_internal::AwaitOrLead;
    using driver_manager_internal::BusKey;
    using driver_manager_internal::CompletePending;
    using driver_manager_internal::PendingCreation;

    if (adapter == nullptr) {
        throw std::invalid_argument("Adapter is null");
    }
    const BusKey key{adapter, raw_bus_idx};
    std::shared_ptr<PendingCreation> publication;
    bool leader = false;
    {
        platform::UniqueLock<platform::Mutex> lock(impl_->object_mutex);
        for (;;) {
            if (impl_->adapter_names.find(adapter) == impl_->adapter_names.end() ||
                impl_->deleting.count(adapter) != 0) {
                throw std::invalid_argument("Adapter is not registered");
            }
            const auto found = impl_->buses.find(key);
            if (found != impl_->buses.end()) {
                auto* bus = found->second;
                if (impl_->deleting.count(bus) == 0) {
                    return bus;
                }
                impl_->ObserveWaitForTests();
                impl_->deletion_condition.wait(lock, [this, &key, bus]() {
                    const auto current = impl_->buses.find(key);
                    return current == impl_->buses.end() || current->second != bus ||
                           impl_->deleting.count(bus) == 0;
                });
                continue;
            }
            if (void* result = AwaitOrLead(lock, impl_->pending_buses, key, publication, leader)) {
                if (impl_->deleting.count(result) == 0) {
                    return static_cast<Bus*>(result);
                }
                continue;
            }
            if (leader) {
                impl_->BeginChildCreationLocked(adapter);
            }
            break;
        }
    }

    Bus* bus = nullptr;
    OperationGate* operation_gate = nullptr;
    bool domain_registered = false;
    try {
        bus = new Bus(adapter, raw_bus_idx, adapter->Logger());
        impl_->InvokeCreationHook(CreationStage::BeforeBusPublish);
        {
            platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
            if (impl_->deleting.count(adapter) != 0) {
                throw std::runtime_error("Adapter is retiring");
            }
            operation_gate = impl_->operation_registry.Register(bus, OperationKind::Bus);
            try {
                impl_->buses.emplace(key, bus);
                impl_->bus_keys.emplace(bus, key);
                {
                    auto& domain = adapter->impl_->route_domain;
                    platform::LockGuard<platform::Mutex> domain_lock(domain.mutex);
                    if (!domain.buses.emplace(raw_bus_idx, bus).second) {
                        throw std::runtime_error("Adapter bus route already registered");
                    }
                    domain_registered = true;
                }
            } catch (...) {
                impl_->buses.erase(key);
                impl_->bus_keys.erase(bus);
                if (domain_registered) {
                    auto& domain = adapter->impl_->route_domain;
                    platform::LockGuard<platform::Mutex> domain_lock(domain.mutex);
                    domain.buses.erase(raw_bus_idx);
                    domain_registered = false;
                }
                operation_gate = impl_->operation_registry.Retire(bus, OperationKind::Bus);
                throw;
            }
        }
        adapter->InitializeBusSyncMode(bus, [this, &key, &publication, bus]() {
            platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
            impl_->pending_buses.erase(key);
            CompletePending(publication, bus);
        });
        impl_->EndChildCreation(adapter);
        return bus;
    } catch (...) {
        {
            platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
            impl_->buses.erase(key);
            impl_->bus_keys.erase(bus);
            if (domain_registered) {
                auto& domain = adapter->impl_->route_domain;
                platform::LockGuard<platform::Mutex> domain_lock(domain.mutex);
                domain.buses.erase(raw_bus_idx);
                domain_registered = false;
            }
            if (bus != nullptr) {
                operation_gate = impl_->operation_registry.Retire(bus, OperationKind::Bus);
            }
            impl_->pending_buses.erase(key);
            CompletePending(publication, nullptr, std::current_exception());
        }
        if (operation_gate != nullptr) {
            operation_gate->WaitForDrain();
            impl_->operation_registry.ReclaimRetired(bus, OperationKind::Bus);
        }
        delete bus;
        impl_->EndChildCreation(adapter);
        throw;
    }
}

Motor* EncosDriverManager::CreateMotor(Bus* bus, int motor_idx, MotorModel model,
                                       uint8_t frame_flags) {
    return CreateMotor(bus, motor_idx, model, frame_flags, CanFrameFlagsUseCanFd(frame_flags));
}

Motor* EncosDriverManager::CreateMotor(Bus* bus, int motor_idx, MotorModel model,
                                       uint8_t frame_flags, bool canfd) {
    using driver_manager_internal::CreateDeviceWithRoutes;
    using driver_manager_internal::DeviceKind;

    return CreateDeviceWithRoutes<Motor>(
        impl_, bus, DeviceKind::Motor, motor_idx,
        [this, bus, motor_idx, model, frame_flags, canfd]() {
            auto writer = impl_->MakeWriter(bus);
            return new Motor(bus, static_cast<std::uint16_t>(motor_idx), model, bus->impl_->logger_,
                             writer, frame_flags, canfd);
        },
        [](Motor* motor) {
            return driver_manager_internal::DeviceRoute{[motor](const MotorPackMsg& message) {
                                                            motor->OnMessage(message);
                                                        },
                                                        [motor]() {
                                                            motor->CancelWaiters();
                                                        }};
        },
        [](Motor*) {},
        [this, bus, motor_idx](Motor* motor) {
            ApplyMotorStatusConfigurationLocked(bus, motor_idx, motor);
        });
}

Motor* EncosDriverManager::CreateMotor(Bus* bus, int motor_idx, MotorPVTRanges ranges,
                                       uint8_t frame_flags) {
    return CreateMotor(bus, motor_idx, ranges, frame_flags, CanFrameFlagsUseCanFd(frame_flags));
}

Motor* EncosDriverManager::CreateMotor(Bus* bus, int motor_idx, MotorPVTRanges ranges,
                                       uint8_t frame_flags, bool canfd) {
    using driver_manager_internal::CreateDeviceWithRoutes;
    using driver_manager_internal::DeviceKind;

    return CreateDeviceWithRoutes<Motor>(
        impl_, bus, DeviceKind::Motor, motor_idx,
        [this, bus, motor_idx, ranges, frame_flags, canfd]() {
            auto writer = impl_->MakeWriter(bus);
            return new Motor(bus, static_cast<std::uint16_t>(motor_idx), ranges,
                             bus->impl_->logger_, writer, frame_flags, canfd);
        },
        [](Motor* motor) {
            return driver_manager_internal::DeviceRoute{[motor](const MotorPackMsg& message) {
                                                            motor->OnMessage(message);
                                                        },
                                                        [motor]() {
                                                            motor->CancelWaiters();
                                                        }};
        },
        [](Motor*) {},
        [this, bus, motor_idx](Motor* motor) {
            ApplyMotorStatusConfigurationLocked(bus, motor_idx, motor);
        });
}

Motor* EncosDriverManager::CreateMotor(Bus* bus, int motor_idx, uint8_t frame_flags) {
    return CreateMotor(bus, motor_idx, frame_flags, CanFrameFlagsUseCanFd(frame_flags));
}

Motor* EncosDriverManager::CreateMotor(Bus* bus, int motor_idx, uint8_t frame_flags, bool canfd) {
    using driver_manager_internal::CreateDeviceWithRoutes;
    using driver_manager_internal::DeviceKind;

    return CreateDeviceWithRoutes<Motor>(
        impl_, bus, DeviceKind::Motor, motor_idx,
        [this, bus, motor_idx, frame_flags, canfd]() {
            auto writer = impl_->MakeWriter(bus);
            return new Motor(bus, static_cast<std::uint16_t>(motor_idx), bus->impl_->logger_,
                             writer, frame_flags, canfd);
        },
        [](Motor* motor) {
            return driver_manager_internal::DeviceRoute{[motor](const MotorPackMsg& message) {
                                                            motor->OnMessage(message);
                                                        },
                                                        [motor]() {
                                                            motor->CancelWaiters();
                                                        }};
        },
        [](Motor* motor) {
            motor->InitMotorPVTParam();
        },
        [this, bus, motor_idx](Motor* motor) {
            ApplyMotorStatusConfigurationLocked(bus, motor_idx, motor);
        });
}

Battery* EncosDriverManager::CreateBattery(Bus* bus, int battery_idx) {
    using driver_manager_internal::CreateDeviceWithRoutes;
    using driver_manager_internal::DeviceKind;

    return CreateDeviceWithRoutes<Battery>(
        impl_, bus, DeviceKind::Battery, battery_idx,
        [this, bus, battery_idx]() {
            auto writer = impl_->MakeWriter(bus);
            return new Battery(bus, static_cast<std::uint16_t>(battery_idx), bus->impl_->logger_,
                               writer);
        },
        [](Battery* battery) {
            return driver_manager_internal::DeviceRoute{[battery](const MotorPackMsg& message) {
                                                            battery->OnMessage(message);
                                                        },
                                                        {}};
        },
        [](Battery*) {}, [](Battery*) {});
}

Imu* EncosDriverManager::CreateImu(Bus* bus, int imu_idx) {
    using driver_manager_internal::CreateDeviceWithRoutes;
    using driver_manager_internal::DeviceKind;

    return CreateDeviceWithRoutes<Imu>(
        impl_, bus, DeviceKind::Imu, imu_idx,
        [this, bus, imu_idx]() {
            DeviceWriteFunction writer;
            return new Imu(bus, static_cast<std::uint16_t>(imu_idx), bus->impl_->logger_, writer);
        },
        [](Imu* imu) {
            return driver_manager_internal::DeviceRoute{[imu](const MotorPackMsg& message) {
                                                            imu->OnMessage(message);
                                                        },
                                                        {}};
        },
        [](Imu*) {}, [](Imu*) {});
}

Pms* EncosDriverManager::CreatePms(Bus* bus) {
    using driver_manager_internal::CreateDeviceWithRoutes;
    using driver_manager_internal::DeviceKind;

    return CreateDeviceWithRoutes<Pms>(
        impl_, bus, DeviceKind::Pms, 0,
        [this, bus]() {
            auto writer = impl_->MakeWriter(bus);
            return new Pms(bus, bus->impl_->logger_, writer);
        },
        [](Pms* pms) {
            return driver_manager_internal::DeviceRoute{[pms](const MotorPackMsg& message) {
                                                            pms->OnMessage(message);
                                                        },
                                                        {}};
        },
        [](Pms*) {}, [](Pms*) {});
}

Glove* EncosDriverManager::CreateGlove(BaseAdapter* adapter, int slave_id) {
    using driver_manager_internal::AwaitOrLead;
    using driver_manager_internal::BusKey;
    using driver_manager_internal::CompletePending;
    using driver_manager_internal::CreateDeviceWithRoutes;
    using driver_manager_internal::DeviceKind;
    using driver_manager_internal::GloveKey;
    using driver_manager_internal::PendingCreation;

    if (adapter == nullptr) {
        throw std::invalid_argument("Adapter is null");
    }
    // 手套整手 = 5 条帧内总线 + 每总线 10 个编码器设备与 1 个虚拟校准设备 +
    // 1 个聚合 facade。编码器与校准设备均为标准设备（经设备模板创建），facade
    // 非设备、不注册路由，仅登记 gloves 表用于幂等与销毁入口。
    const GloveKey glove_key{adapter, slave_id};
    std::array<BusKey, 5> glove_bus_keys{};
    for (std::size_t finger = 0; finger < glove_bus_keys.size(); ++finger) {
        glove_bus_keys[finger] =
            BusKey{adapter, (slave_id << 16) | (static_cast<int>(finger) & 0xFF)};
    }
    std::shared_ptr<PendingCreation> publication;
    bool leader = false;
    bool glove_bus_keys_reserved = false;
    std::exception_ptr glove_bus_reservation_error;
    {
        platform::UniqueLock<platform::Mutex> lock(impl_->object_mutex);
        for (;;) {
            if (impl_->adapter_names.find(adapter) == impl_->adapter_names.end() ||
                impl_->deleting.count(adapter) != 0) {
                throw std::invalid_argument("Adapter is not registered");
            }

            const auto found = impl_->gloves.find(glove_key);
            if (found != impl_->gloves.end()) {
                Glove* existing = found->second;
                if (impl_->deleting_gloves.count(existing) == 0) {
                    return existing;
                }
                impl_->ObserveWaitForTests();
                impl_->deletion_condition.wait(lock, [this, &glove_key, existing]() {
                    const auto current = impl_->gloves.find(glove_key);
                    return current == impl_->gloves.end() || current->second != existing ||
                           impl_->deleting_gloves.count(existing) == 0;
                });
                continue;
            }

            if (void* result =
                    AwaitOrLead(lock, impl_->pending_gloves, glove_key, publication, leader)) {
                if (impl_->deleting_gloves.count(static_cast<Glove*>(result)) == 0) {
                    return static_cast<Glove*>(result);
                }
                continue;
            }
            if (leader) {
                impl_->BeginChildCreationLocked(adapter);
                try {
                    for (const auto& bus_key : glove_bus_keys) {
                        impl_->pending_glove_bus_keys.insert(bus_key);
                    }
                    glove_bus_keys_reserved = true;
                } catch (...) {
                    for (const auto& bus_key : glove_bus_keys) {
                        impl_->pending_glove_bus_keys.erase(bus_key);
                    }
                    impl_->pending_gloves.erase(glove_key);
                    glove_bus_reservation_error = std::current_exception();
                    CompletePending(publication, nullptr, glove_bus_reservation_error);
                }
                break;
            }
        }
    }

    if (glove_bus_reservation_error != nullptr) {
        impl_->EndChildCreation(adapter);
        std::rethrow_exception(glove_bus_reservation_error);
    }

    std::array<Bus*, 5> buses{};
    std::array<GloveEncoder*, 50> encoders{};
    std::array<GloveCalibrator*, 5> calibrators{};
    std::size_t buses_created = 0;
    Glove* facade = nullptr;
    OperationGate* facade_operation_gate = nullptr;
    try {
        for (std::size_t finger = 0; finger < 5; ++finger) {
            Bus* sub_bus = adapter->GetBus(slave_id, static_cast<int>(finger));
            buses[finger] = sub_bus;
            ++buses_created;
            // 手套从站的 5 条子总线均为外部设备总线，禁止电机扫描。
            sub_bus->impl_->SetExternalDeviceFlag(true);
            for (std::size_t encoder = 0; encoder < 10; ++encoder) {
                const auto global_idx = static_cast<uint8_t>(finger * 10 + encoder);
                encoders[global_idx] = CreateDeviceWithRoutes<GloveEncoder>(
                    impl_, sub_bus, DeviceKind::GloveEncoder, global_idx,
                    [global_idx]() {
                        return new GloveEncoder(global_idx);
                    },
                    [](GloveEncoder*) {
                        return driver_manager_internal::DeviceRoute{{}, {}};
                    },
                    [](GloveEncoder*) {}, [](GloveEncoder*) {});
            }
            calibrators[finger] = CreateDeviceWithRoutes<GloveCalibrator>(
                impl_, sub_bus, DeviceKind::GloveCalibrator, static_cast<int>(finger),
                [this, sub_bus]() {
                    return new GloveCalibrator(impl_->MakeWriter(sub_bus));
                },
                [](GloveCalibrator*) {
                    return driver_manager_internal::DeviceRoute{{}, {}};
                },
                [](GloveCalibrator*) {}, [](GloveCalibrator*) {});
        }
        // 仅测试钩子会在此暂停。并发 GetGlove 必须等待 pending_gloves，而不能在此时
        // 从已创建设备反查 facade。
        impl_->InvokeCreationHook(CreationStage::BeforeGloveFacadePublish);
        facade = new Glove(buses);
        // 设备只引用 facade 持有的内部状态，不保存或间接捕获 Glove facade。
        auto* glove_impl = facade->impl_.get();
        for (std::size_t idx = 0; idx < encoders.size(); ++idx) {
            facade->impl_->encoders[idx] = encoders[idx];
            encoders[idx]->ConnectStatusCallback(
                [glove_impl](std::size_t encoder_idx, GloveEncoderStatus status) {
                    glove_impl->OnEncoderUpdate(encoder_idx, status);
                });
        }
        for (std::size_t finger = 0; finger < calibrators.size(); ++finger) {
            facade->impl_->calibrators[finger] = calibrators[finger];
        }
        impl_->InvokeCreationHook(CreationStage::AfterGloveCallbacksConnected);
        for (std::size_t idx = 0; idx < encoders.size(); ++idx) {
            if (!RegisterReceiveRoutes(
                    encoders[idx], adapter, buses[idx / 10u],
                    impl_->RouteIds(DeviceKind::GloveEncoder, static_cast<int>(idx)),
                    [encoder_device = encoders[idx]](const MotorPackMsg& message) {
                        encoder_device->OnMessage(message);
                    })) {
                throw std::runtime_error("Failed to activate glove encoder receive route");
            }
        }
        for (std::size_t finger = 0; finger < calibrators.size(); ++finger) {
            if (!RegisterReceiveRoutes(
                    calibrators[finger], adapter, buses[finger],
                    impl_->RouteIds(DeviceKind::GloveCalibrator, static_cast<int>(finger)),
                    [calibrator_device = calibrators[finger]](const MotorPackMsg& message) {
                        calibrator_device->OnMessage(message);
                    })) {
                throw std::runtime_error("Failed to activate glove calibrator receive route");
            }
        }
        {
            platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
            if (impl_->deleting.count(adapter) != 0) {
                throw std::runtime_error("Adapter is retiring");
            }
            try {
                for (auto* glove_bus : buses) {
                    impl_->glove_internal_buses.insert(glove_bus);
                }
                facade_operation_gate =
                    impl_->operation_registry.Register(facade, OperationKind::Glove);
                impl_->gloves.emplace(glove_key, facade);
                impl_->glove_keys.emplace(facade, glove_key);
                for (const auto& bus_key : glove_bus_keys) {
                    impl_->pending_glove_bus_keys.erase(bus_key);
                }
            } catch (...) {
                impl_->gloves.erase(glove_key);
                impl_->glove_keys.erase(facade);
                for (auto* glove_bus : buses) {
                    impl_->glove_internal_buses.erase(glove_bus);
                }
                if (facade_operation_gate != nullptr) {
                    facade_operation_gate =
                        impl_->operation_registry.Retire(facade, OperationKind::Glove);
                }
                throw;
            }
            impl_->pending_gloves.erase(glove_key);
            CompletePending(publication, facade);
        }
        impl_->EndChildCreation(adapter);
        return facade;
    } catch (...) {
        if (facade_operation_gate != nullptr) {
            facade_operation_gate = impl_->operation_registry.Retire(facade, OperationKind::Glove);
        }
        // 每个内部路由均会在设备销毁前排空，确保其回调不会越过 Impl 生命周期。
        if (facade_operation_gate != nullptr) {
            facade_operation_gate->WaitForDrain();
            impl_->operation_registry.ReclaimRetired(facade, OperationKind::Glove);
        }
        for (std::size_t finger = 0; finger < buses_created; ++finger) {
            if (buses[finger] == nullptr) {
                continue;
            }
            if (!DestroyGloveInternalBus(buses[finger])) {
                impl_->WaitForBusChildren(buses[finger]);
            }
        }
        delete facade;
        {
            platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
            impl_->gloves.erase(glove_key);
            impl_->glove_keys.erase(facade);
            if (glove_bus_keys_reserved) {
                for (const auto& bus_key : glove_bus_keys) {
                    impl_->pending_glove_bus_keys.erase(bus_key);
                }
            }
            for (auto* glove_bus : buses) {
                impl_->glove_internal_buses.erase(glove_bus);
            }
            impl_->pending_gloves.erase(glove_key);
            CompletePending(publication, nullptr, std::current_exception());
        }
        impl_->EndChildCreation(adapter);
        throw;
    }
}

}  // namespace encos
