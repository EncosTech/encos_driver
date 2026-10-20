// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <cstdio>
#include <utility>

#include "driver_manager_impl.h"

namespace encos {
namespace {

OperationGate* TryEnterRequired(OperationRegistry& registry, void* object, OperationKind kind) {
    const auto result = registry.TryEnterDetailed(object, kind);
    if (result.failure == OperationEnterFailure::HazardCapacityExhausted) {
        throw std::runtime_error("Operation registry hazard capacity exhausted");
    }
    return result.gate;
}

}  // namespace

EncosDriverManager& EncosDriverManager::Instance() {
    static EncosDriverManager instance;
    return instance;
}

EncosDriverManager::EncosDriverManager() : impl_(new Impl()) {}

EncosDriverManager::~EncosDriverManager() noexcept {
    {
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        impl_->contain_deletion_hook_exceptions = true;
    }
    try {
        DestroyAllManagedObjects();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "EncosDriverManager shutdown cleanup failed: %s\n", error.what());
    } catch (...) {
        std::fputs("EncosDriverManager shutdown cleanup failed with an unknown exception\n",
                   stderr);
    }
    if (impl_->deletion_hook_exception_observed) {
        std::fputs("EncosDriverManager ignored a deletion test-hook exception during shutdown\n",
                   stderr);
    }
    delete impl_;
}

EncosDriverManager::DeviceOperation*& EncosDriverManager::DeviceOperation::ActiveHead() {
    static thread_local DeviceOperation* active_head = nullptr;
    return active_head;
}

EncosDriverManager::DeviceOperation::DeviceOperation(OperationGate* gate, bool required) {
    DeviceOperation*& active_head_ = ActiveHead();
    if (gate == nullptr) {
        if (required) {
            throw std::runtime_error("Object is not registered or is retiring");
        }
        return;
    }
    for (auto* active = active_head_; active != nullptr; active = active->previous_) {
        if (active->gate_ == gate) {
            gate->Leave();
            gate_ = gate;
            previous_ = active_head_;
            active_head_ = this;
            return;
        }
    }
    gate_ = gate;
    previous_ = active_head_;
    owns_admission_ = true;
    active_head_ = this;
}

EncosDriverManager::DeviceOperation::~DeviceOperation() {
    if (gate_ == nullptr) {
        return;
    }
    DeviceOperation*& active_head_ = ActiveHead();
    if (active_head_ != this) {
        std::terminate();
    }
    active_head_ = previous_;
    if (owns_admission_) {
        gate_->Leave();
    }
}

EncosDriverManager::DeviceOperation EncosDriverManager::AcquireDeviceOperation(Motor* device) {
    return DeviceOperation(
        TryEnterRequired(impl_->operation_registry, device, OperationKind::Motor), true);
}

EncosDriverManager::DeviceOperation EncosDriverManager::AcquireDeviceOperation(Battery* device) {
    return DeviceOperation(
        TryEnterRequired(impl_->operation_registry, device, OperationKind::Battery), true);
}

EncosDriverManager::DeviceOperation EncosDriverManager::AcquireDeviceOperation(Imu* device) {
    return DeviceOperation(TryEnterRequired(impl_->operation_registry, device, OperationKind::Imu),
                           true);
}

EncosDriverManager::DeviceOperation EncosDriverManager::AcquireDeviceOperation(Pms* device) {
    return DeviceOperation(TryEnterRequired(impl_->operation_registry, device, OperationKind::Pms),
                           true);
}

EncosDriverManager::DeviceOperation EncosDriverManager::AcquireDeviceOperation(Device* device) {
    return DeviceOperation(
        TryEnterRequired(impl_->operation_registry, device, OperationKind::ExternalDevice), true);
}

EncosDriverManager::DeviceOperation EncosDriverManager::TryAcquireGloveOperation(
    Glove* glove) noexcept {
    return DeviceOperation(impl_->operation_registry.TryEnter(glove, OperationKind::Glove), false);
}

EncosDriverManager::DeviceOperation EncosDriverManager::AcquireDeviceOperation(
    GloveEncoder* device) {
    return DeviceOperation(
        TryEnterRequired(impl_->operation_registry, device, OperationKind::GloveEncoder), true);
}

EncosDriverManager::DeviceOperation EncosDriverManager::AcquireDeviceOperation(
    GloveCalibrator* device) {
    return DeviceOperation(
        TryEnterRequired(impl_->operation_registry, device, OperationKind::GloveCalibrator), true);
}

EncosDriverManager::DeviceOperation EncosDriverManager::AcquireBusOperation(Bus* bus) {
    return DeviceOperation(TryEnterRequired(impl_->operation_registry, bus, OperationKind::Bus),
                           true);
}

EncosDriverManager::DeviceOperation EncosDriverManager::TryAcquireBusOperation(Bus* bus) noexcept {
    return DeviceOperation(impl_->operation_registry.TryEnter(bus, OperationKind::Bus), false);
}

EncosDriverManager::DeviceOperation EncosDriverManager::TryAcquireAdapterReceive(
    BaseAdapter* adapter) noexcept {
    return DeviceOperation(impl_->operation_registry.TryEnter(adapter, OperationKind::Adapter),
                           false);
}

BaseAdapter* EncosDriverManager::CreateAdapterWithFactory(const std::string& interface_name,
                                                          AdapterFactory factory) {
    return CreateAdapterWithFactory(interface_name, std::string{}, std::move(factory));
}

BaseAdapter* EncosDriverManager::CreateAdapterWithFactory(const std::string& interface_name,
                                                          const std::string& extend_json,
                                                          AdapterFactory factory) {
    using driver_manager_internal::AwaitOrLead;
    using driver_manager_internal::CompletePending;
    using driver_manager_internal::PendingCreation;

    std::shared_ptr<PendingCreation> publication;
    bool leader = false;
    BaseAdapter* existing = nullptr;
    OperationGate* gate = nullptr;
    {
        platform::UniqueLock<platform::Mutex> lock(impl_->object_mutex);
        for (;;) {
            const auto found = impl_->adapters.find(interface_name);
            if (found != impl_->adapters.end()) {
                auto* adapter = found->second;
                if (impl_->deleting.count(adapter) == 0) {
                    existing = adapter;
                    break;
                }
                impl_->ObserveWaitForTests();
                impl_->deletion_condition.wait(lock, [this, &interface_name, adapter]() {
                    const auto current = impl_->adapters.find(interface_name);
                    return current == impl_->adapters.end() || current->second != adapter ||
                           impl_->deleting.count(adapter) == 0;
                });
                continue;
            }
            if (AwaitOrLead(lock, impl_->pending_adapters, interface_name, publication, leader) !=
                nullptr) {
                // 等待期间已发布的实例可能又被销毁，重新查询当前登记实例。
                continue;
            }
            break;
        }
        if (existing != nullptr && !extend_json.empty()) {
            gate = TryEnterRequired(impl_->operation_registry, existing, OperationKind::Adapter);
            if (gate == nullptr) {
                throw std::runtime_error("Adapter '" + interface_name +
                                         "' is being destroyed; retry MakeAdapter");
            }
        }
    }
    if (existing != nullptr) {
        if (extend_json.empty()) {
            return existing;
        }
        // 增量更新必须在对象锁之外执行：一次更新可能阻塞数秒，而且插件可能回调管理器
        // 取同一把锁（例如 Ethernet 的按通道重配会经 GetBus/GetMotors 回到管理器）。
        // 以适配器自身的在飞引用把生命周期钉住，DestroyAdapter 会在排空处等待。
        struct GateLeaver {
            OperationGate* gate;
            ~GateLeaver() {
                gate->Leave();
            }
        } leaver{gate};
        platform::LockGuard<platform::Mutex> update_lock(impl_->adapter_update_mutex);
        existing->UpdateBusExtend(extend_json);
        return existing;
    }
    if (!leader) {
        return static_cast<BaseAdapter*>(publication->result);
    }

    BaseAdapter* adapter = nullptr;
    OperationGate* operation_gate = nullptr;
    try {
        adapter = factory();
        if (adapter == nullptr) {
            throw std::runtime_error("Adapter factory returned null");
        }
        if (adapter->GetInterfaceName() != interface_name) {
            throw std::invalid_argument("Adapter interface identity mismatch");
        }
        {
            platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
            operation_gate = impl_->operation_registry.Register(adapter, OperationKind::Adapter);
            try {
                impl_->adapters.emplace(interface_name, adapter);
                impl_->adapter_names.emplace(adapter, interface_name);
            } catch (...) {
                impl_->adapters.erase(interface_name);
                impl_->adapter_names.erase(adapter);
                operation_gate = impl_->operation_registry.Retire(adapter, OperationKind::Adapter);
                throw;
            }
            impl_->pending_adapters.erase(interface_name);
            CompletePending(publication, adapter);
        }
        return adapter;
    } catch (...) {
        if (operation_gate != nullptr) {
            operation_gate->Retire();
            operation_gate->WaitForDrain();
            impl_->operation_registry.ReclaimRetired(adapter, OperationKind::Adapter);
        }
        delete adapter;
        platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
        impl_->pending_adapters.erase(interface_name);
        CompletePending(publication, nullptr, std::current_exception());
        throw;
    }
}

void EncosDriverManager::ResetForTests() {
    DestroyAllManagedObjects();
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    impl_->route_publish_hook = {};
    impl_->child_creation_hook = {};
    impl_->glove_activation_hook = {};
}

void EncosDriverManager::SetCreationHookForTests(CreationHook hook) {
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    impl_->creation_hook = std::move(hook);
}

void EncosDriverManager::SetDeviceInitializerHookForTests(DeviceInitializerHook hook) {
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    impl_->device_initializer_hook = std::move(hook);
}

void EncosDriverManager::SetDeletionHookForTests(DeletionHook hook) {
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    impl_->deletion_hook = std::move(hook);
}

void EncosDriverManager::SetMigrationHookForTests(MigrationHook hook) {
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    impl_->migration_hook = std::move(hook);
}

void EncosDriverManager::SetWaitHookForTests(std::function<void()> hook) {
    platform::LockGuard<platform::Mutex> lock(impl_->object_mutex);
    impl_->wait_hook = std::move(hook);
}

void EncosDriverManager::RunWithSlowPathLocksForTests(const std::function<void()>& callback) {
    std::scoped_lock lock(impl_->object_mutex, impl_->route_mutex);
    callback();
}

}  // namespace encos
