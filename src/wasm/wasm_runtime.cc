// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "wasm/wasm_runtime.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

#include "utils/scope_exit.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace encos::wasm {

namespace {

thread_local RuntimeStore* thread_store = nullptr;

std::string DefaultLoggerName(const std::string& adapter_type, const std::string& logger_name) {
    if (!logger_name.empty()) {
        return logger_name;
    }
    return adapter_type + "Adapter";
}

}  // namespace

RuntimeStore& Store() {
    if (thread_store != nullptr) {
        return *thread_store;
    }
    static RuntimeStore store;
    return store;
}

RuntimeStore* SetThreadStore(RuntimeStore* store) {
    auto* previous = thread_store;
    thread_store = store;
    return previous;
}

void RuntimeStore::DisposeAll() {
    std::vector<std::uint32_t> handles;
    for (const auto& [handle, entry] : adapters_) {
        (void) entry;
        handles.push_back(handle);
    }
    for (auto handle : handles) {
        DisposeAdapter(handle);
    }
}

ErrorCode SetResultError(ErrorCode code, const std::string& message) {
    Store().SetLastError(code, message);
    return code;
}

void SetOk() {
    Store().ClearLastError();
}

std::uint32_t RuntimeStore::AllocateHandle() {
    if (next_handle_ == std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("WASM handle space exhausted");
    }
    return next_handle_++;
}

Result<std::uint32_t> RuntimeStore::CreateAdapter(const std::string& adapter_type,
                                                  const std::string& interface_name,
                                                  const std::string& logger_name,
                                                  LogLevel log_level,
                                                  const nlohmann::json& extend) {
    if (adapter_type.empty()) {
        return Result<std::uint32_t>::Failure(ErrorCode::InvalidArgument,
                                              "adapter_type must not be empty");
    }
    if (pending_adapter_interfaces_.count(interface_name) != 0) {
        return Result<std::uint32_t>::Failure(ErrorCode::Disposed,
                                              "adapter interface is pending disposal");
    }
    AdapterEntry entry;
    entry.interface_name = interface_name;
    entry.valid = false;
    const auto handle = AllocateHandle();
    const auto [entry_it, inserted] = adapters_.emplace(handle, std::move(entry));
    if (!inserted || !adapter_handle_leases_.emplace(nullptr, 0).second) {
        adapters_.erase(handle);
        return Result<std::uint32_t>::Failure(ErrorCode::InternalError,
                                              "failed to reserve adapter handle storage");
    }

    BaseAdapter* adapter = nullptr;
    try {
        adapter = EncosDriverManager::Instance().CreateAdapter(adapter_type, interface_name,
                                                               logger_name, log_level, extend);
    } catch (...) {
        adapter_handle_leases_.erase(nullptr);
        adapters_.erase(entry_it);
        throw;
    }
    if (adapter == nullptr) {
        adapter_handle_leases_.erase(nullptr);
        adapters_.erase(entry_it);
        return Result<std::uint32_t>::Failure(ErrorCode::OperationFailed,
                                              "MakeAdapter returned null");
    }

    auto lease_it = adapter_handle_leases_.find(adapter);
    if (lease_it == adapter_handle_leases_.end()) {
        auto lease_node = adapter_handle_leases_.extract(nullptr);
        lease_node.key() = adapter;
        lease_node.mapped() = 1;
        adapter_handle_leases_.insert(std::move(lease_node));
    } else {
        adapter_handle_leases_.erase(nullptr);
        ++lease_it->second;
    }
    entry_it->second.adapter = adapter;
    entry_it->second.valid = true;
    return Result<std::uint32_t>::Success(handle);
}

Result<std::uint32_t> RuntimeStore::CreateFakeAdapter(const std::string& interface_name,
                                                      const std::string& logger_name,
                                                      LogLevel log_level) {
    if (pending_adapter_interfaces_.count(interface_name) != 0) {
        return Result<std::uint32_t>::Failure(ErrorCode::Disposed,
                                              "adapter interface is pending disposal");
    }
    AdapterEntry entry;
    entry.interface_name = interface_name;
    entry.fake_tools_enabled = true;
    entry.valid = false;
    const auto handle = AllocateHandle();
    const auto [entry_it, inserted] = adapters_.emplace(handle, std::move(entry));
    if (!inserted || !adapter_handle_leases_.emplace(nullptr, 0).second) {
        adapters_.erase(handle);
        return Result<std::uint32_t>::Failure(ErrorCode::InternalError,
                                              "failed to reserve adapter handle storage");
    }

    BaseAdapter* adapter = nullptr;
    try {
        adapter = EncosDriverManager::Instance().CreateAdapterWithFactory(
            interface_name, [interface_name, logger_name, log_level]() {
                return new FakeAdapter(interface_name, DefaultLoggerName("Fake", logger_name),
                                       log_level);
            });
    } catch (...) {
        adapter_handle_leases_.erase(nullptr);
        adapters_.erase(entry_it);
        throw;
    }
    auto* fake = dynamic_cast<FakeAdapter*>(adapter);
    if (fake == nullptr) {
        adapter_handle_leases_.erase(nullptr);
        adapters_.erase(entry_it);
        return Result<std::uint32_t>::Failure(ErrorCode::WrongAdapterType,
                                              "interface is not managed as a Fake adapter");
    }

    auto lease_it = adapter_handle_leases_.find(adapter);
    if (lease_it == adapter_handle_leases_.end()) {
        auto lease_node = adapter_handle_leases_.extract(nullptr);
        lease_node.key() = adapter;
        lease_node.mapped() = 1;
        adapter_handle_leases_.insert(std::move(lease_node));
    } else {
        adapter_handle_leases_.erase(nullptr);
        ++lease_it->second;
    }
    entry_it->second.adapter = adapter;
    entry_it->second.fake_adapter = fake;
    entry_it->second.valid = true;
    return Result<std::uint32_t>::Success(handle);
}

ErrorCode RuntimeStore::DisposeAdapter(std::uint32_t adapter_handle) {
    auto adapter_it = adapters_.find(adapter_handle);
    if (adapter_it != adapters_.end() && !adapter_it->second.valid &&
        adapter_it->second.disposing) {
        const auto pending_it = pending_adapter_deletions_.find(adapter_it->second.adapter);
        if (pending_it == pending_adapter_deletions_.end() ||
            pending_it->second.adapter_handle != adapter_handle) {
            return ErrorCode::InvalidHandle;
        }
        if (!pending_it->second.exhausted) {
            return ErrorCode::Ok;
        }
        pending_it->second.exhausted = false;
        pending_it->second.retry_count = 0;
        pending_it->second.retry_delay_ms = 1;
        const int retry_delay_ms = PollPendingAdapterDeletionsOnce();
#ifdef __EMSCRIPTEN__
        if (retry_delay_ms > 0) {
            emscripten_async_call(&RuntimeStore::PollPendingAdapterDeletionsCallback, this,
                                  retry_delay_ms);
        }
#else
        (void) retry_delay_ms;
#endif
        return ErrorCode::Ok;
    }
    if (adapter_it == adapters_.end() || !adapter_it->second.valid ||
        adapter_it->second.disposing) {
        return ErrorCode::InvalidHandle;
    }

    if (motor_creation_adapter_ == adapter_it->second.adapter) {
        return ErrorCode::OperationFailed;
    }
    auto& entry = adapter_it->second;
    entry.disposing = true;
    std::vector<decltype(motors_)::node_type> removed_motors;
    std::vector<decltype(batteries_)::node_type> removed_batteries;
    std::vector<decltype(imus_)::node_type> removed_imus;
    std::vector<decltype(buses_)::node_type> removed_buses;
    for (const auto motor_handle : entry.child_motor_handles) {
        auto node = motors_.extract(motor_handle);
        if (!node.empty()) {
            node.mapped().valid = false;
            removed_motors.push_back(std::move(node));
        }
    }
    for (const auto battery_handle : entry.child_battery_handles) {
        auto node = batteries_.extract(battery_handle);
        if (!node.empty()) {
            node.mapped().valid = false;
            removed_batteries.push_back(std::move(node));
        }
    }
    for (const auto imu_handle : entry.child_imu_handles) {
        auto node = imus_.extract(imu_handle);
        if (!node.empty()) {
            node.mapped().valid = false;
            removed_imus.push_back(std::move(node));
        }
    }
    for (const auto bus_handle : entry.child_bus_handles) {
        auto node = buses_.extract(bus_handle);
        if (!node.empty()) {
            node.mapped().valid = false;
            removed_buses.push_back(std::move(node));
        }
    }
    const auto restore_children = [&]() {
        for (auto& node : removed_motors) {
            node.mapped().valid = true;
            motors_.insert(std::move(node));
        }
        for (auto& node : removed_batteries) {
            node.mapped().valid = true;
            batteries_.insert(std::move(node));
        }
        for (auto& node : removed_imus) {
            node.mapped().valid = true;
            imus_.insert(std::move(node));
        }
        for (auto& node : removed_buses) {
            node.mapped().valid = true;
            buses_.insert(std::move(node));
        }
    };
    auto* adapter = entry.adapter;
    const auto lease_it = adapter_handle_leases_.find(adapter);
    if (lease_it == adapter_handle_leases_.end() || lease_it->second == 0) {
        restore_children();
        entry.disposing = false;
        return ErrorCode::InternalError;
    }
    if (lease_it->second > 1) {
        --lease_it->second;
        entry.valid = false;
        adapters_.erase(adapter_it);
        return ErrorCode::Ok;
    }

#ifdef __EMSCRIPTEN__
    {
        PendingAdapterDeletion pending;
        pending.interface_name = entry.interface_name;
        pending.adapter = adapter;
        auto& manager = EncosDriverManager::Instance();
        for (const auto& [raw_idx, bus] : manager.GetBuses(adapter)) {
            (void) raw_idx;
            for (const auto& [motor_idx, motor] : manager.GetMotors(bus)) {
                (void) motor_idx;
                motor->CancelWaitersWithoutDrain();
                pending.motors.push_back(motor);
            }
        }
        const bool drained =
            std::all_of(pending.motors.begin(), pending.motors.end(), [](Motor* motor) {
                return motor->WaitersDrained();
            });
        if (!drained) {
            if (!BeginPendingAdapterDeletion(adapter_handle, std::move(pending.motors))) {
                restore_children();
                entry.disposing = false;
                return ErrorCode::InternalError;
            }
            emscripten_async_call(&RuntimeStore::PollPendingAdapterDeletionsCallback, this, 0);
            return ErrorCode::Ok;
        }
    }
#endif
    if (!EncosDriverManager::Instance().DestroyAdapter(adapter)) {
        restore_children();
        entry.disposing = false;
        return ErrorCode::OperationFailed;
    }
    adapter_handle_leases_.erase(lease_it);
    entry.valid = false;
    adapters_.erase(adapter_it);
    return ErrorCode::Ok;
}

bool RuntimeStore::BeginPendingAdapterDeletion(std::uint32_t adapter_handle,
                                               std::vector<Motor*> motors) {
    const auto adapter_it = adapters_.find(adapter_handle);
    if (adapter_it == adapters_.end() || adapter_it->second.adapter == nullptr ||
        pending_adapter_deletions_.count(adapter_it->second.adapter) != 0) {
        return false;
    }
    const auto lease_it = adapter_handle_leases_.find(adapter_it->second.adapter);
    if (lease_it == adapter_handle_leases_.end() || lease_it->second != 1) {
        return false;
    }

    PendingAdapterDeletion pending;
    pending.adapter_handle = adapter_handle;
    pending.interface_name = adapter_it->second.interface_name;
    pending.adapter = adapter_it->second.adapter;
    pending.motors = std::move(motors);
    const auto [pending_it, inserted] =
        pending_adapter_deletions_.emplace(pending.adapter, std::move(pending));
    if (!inserted) {
        return false;
    }
    try {
        pending_adapter_interfaces_.insert(pending_it->second.interface_name);
    } catch (...) {
        pending_adapter_deletions_.erase(pending_it);
        throw;
    }
    adapter_it->second.valid = false;
    adapter_it->second.disposing = true;
    return true;
}

int RuntimeStore::PollPendingAdapterDeletionsOnce() {
    constexpr std::uint32_t kMaximumDeletionRetries = 64;
    bool needs_retry = false;
    int next_delay_ms = 64;
    for (auto it = pending_adapter_deletions_.begin(); it != pending_adapter_deletions_.end();) {
        if (it->second.exhausted) {
            ++it;
            continue;
        }
        const bool drained =
            std::all_of(it->second.motors.begin(), it->second.motors.end(), [](Motor* motor) {
                return motor->WaitersDrained();
            });
        if (!drained || !EncosDriverManager::Instance().DestroyAdapter(it->second.adapter)) {
            if (++it->second.retry_count >= kMaximumDeletionRetries) {
                it->second.exhausted = true;
                if (logger_ != nullptr) {
                    logger_->error(
                        "Deferred WASM adapter disposal for interface '{}' exhausted retries; "
                        "the interface and lease remain reserved until dispose is retried",
                        it->second.interface_name);
                }
                ++it;
                continue;
            }
            needs_retry = true;
            it->second.retry_delay_ms = std::min(it->second.retry_delay_ms * 2, 64);
            next_delay_ms = std::min(next_delay_ms, it->second.retry_delay_ms);
            ++it;
            continue;
        }
        const auto adapter = it->second.adapter;
        const auto adapter_handle = it->second.adapter_handle;
        pending_adapter_interfaces_.erase(it->second.interface_name);
        it = pending_adapter_deletions_.erase(it);
        adapter_handle_leases_.erase(adapter);
        adapters_.erase(adapter_handle);
    }
    return needs_retry ? next_delay_ms : 0;
}

#ifdef __EMSCRIPTEN__
void RuntimeStore::PollPendingAdapterDeletionsCallback(void* context) {
    static_cast<RuntimeStore*>(context)->PollPendingAdapterDeletions();
}

void RuntimeStore::PollPendingAdapterDeletions() {
    const int next_delay_ms = PollPendingAdapterDeletionsOnce();
    if (next_delay_ms > 0) {
        emscripten_async_call(&RuntimeStore::PollPendingAdapterDeletionsCallback, this,
                              next_delay_ms);
    }
}
#endif

Result<std::uint32_t> RuntimeStore::GetBus(std::uint32_t adapter_handle, int slave_idx,
                                           int bus_idx) {
    auto adapter_result = ResolveAdapter(adapter_handle);
    if (!adapter_result.Ok()) {
        return Result<std::uint32_t>::Failure(adapter_result.code, adapter_result.message);
    }
    auto& adapter = *adapter_result.value;
    auto bus = slave_idx < 0 ? adapter.adapter->GetBus(bus_idx)
                             : adapter.adapter->GetBus(slave_idx, bus_idx);
    if (!bus) {
        return Result<std::uint32_t>::Failure(ErrorCode::OperationFailed, "GetBus returned null");
    }

    const int raw_idx = bus->GetBusIndex();
    auto existing = adapter.bus_handles_by_raw_index.find(raw_idx);
    if (existing != adapter.bus_handles_by_raw_index.end()) {
        return Result<std::uint32_t>::Success(existing->second);
    }

    BusEntry entry;
    entry.adapter_handle = adapter_handle;
    entry.bus = bus;
    entry.key = BusKey{raw_idx, slave_idx, bus_idx};
    const auto handle = AllocateHandle();
    buses_[handle] = std::move(entry);
    adapter.bus_handles_by_raw_index[raw_idx] = handle;
    adapter.child_bus_handles.push_back(handle);
    return Result<std::uint32_t>::Success(handle);
}

Result<std::vector<int>> RuntimeStore::ListBusRawIndices(std::uint32_t adapter_handle) {
    auto adapter_result = ResolveAdapter(adapter_handle);
    if (!adapter_result.Ok()) {
        return Result<std::vector<int>>::Failure(adapter_result.code, adapter_result.message);
    }
    auto buses = adapter_result.value->adapter->GetBuses();
    std::vector<int> raw_indices;
    raw_indices.reserve(buses.size());
    for (const auto& [raw_idx, _] : buses) {
        raw_indices.push_back(raw_idx);
    }
    std::sort(raw_indices.begin(), raw_indices.end());
    return Result<std::vector<int>>::Success(std::move(raw_indices));
}

Result<std::uint32_t> RuntimeStore::GetMotorWithModel(std::uint32_t bus_handle, int motor_idx,
                                                      MotorModel model) {
    auto bus_result = ResolveBus(bus_handle);
    if (!bus_result.Ok()) {
        return Result<std::uint32_t>::Failure(bus_result.code, bus_result.message);
    }
    auto& bus_entry = *bus_result.value;
    auto existing = bus_entry.motor_handles_by_index.find(motor_idx);
    if (existing != bus_entry.motor_handles_by_index.end()) {
        return Result<std::uint32_t>::Success(existing->second);
    }

    if (motor_creation_adapter_ != nullptr) {
        return Result<std::uint32_t>::Failure(ErrorCode::OperationFailed,
                                              "motor creation is in progress");
    }
    auto adapter = ResolveAdapter(bus_entry.adapter_handle);
    if (!adapter.Ok()) {
        return Result<std::uint32_t>::Failure(adapter.code, adapter.message);
    }
    motor_creation_adapter_ = adapter.value->adapter;
    auto finish_creation = utils::MakeScopeExit([this]() {
        motor_creation_adapter_ = nullptr;
    });
    auto motor = bus_entry.bus->GetMotor(motor_idx, model);
    if (!motor) {
        return Result<std::uint32_t>::Failure(ErrorCode::OperationFailed, "GetMotor returned null");
    }

    MotorEntry entry;
    entry.adapter_handle = bus_entry.adapter_handle;
    entry.bus_handle = bus_handle;
    entry.motor_idx = motor_idx;
    entry.motor = motor;
    const auto handle = AllocateHandle();
    motors_[handle] = entry;
    bus_entry.motor_handles_by_index[motor_idx] = handle;
    auto adapter_result = ResolveAdapter(bus_entry.adapter_handle);
    if (adapter_result.Ok()) {
        adapter_result.value->child_motor_handles.push_back(handle);
    }
    return Result<std::uint32_t>::Success(handle);
}

Result<std::uint32_t> RuntimeStore::GetBattery(std::uint32_t bus_handle, int battery_idx) {
    auto bus_result = ResolveBus(bus_handle);
    if (!bus_result.Ok()) {
        return Result<std::uint32_t>::Failure(bus_result.code, bus_result.message);
    }
    auto& bus_entry = *bus_result.value;
    auto existing = bus_entry.battery_handles_by_index.find(battery_idx);
    if (existing != bus_entry.battery_handles_by_index.end()) {
        return Result<std::uint32_t>::Success(existing->second);
    }

    auto battery = bus_entry.bus->GetBattery(battery_idx);
    if (!battery) {
        return Result<std::uint32_t>::Failure(ErrorCode::OperationFailed,
                                              "GetBattery returned null");
    }

    BatteryEntry entry;
    entry.adapter_handle = bus_entry.adapter_handle;
    entry.bus_handle = bus_handle;
    entry.battery_idx = battery_idx;
    entry.battery = battery;
    const auto handle = AllocateHandle();
    batteries_[handle] = entry;
    bus_entry.battery_handles_by_index[battery_idx] = handle;
    auto adapter_result = ResolveAdapter(bus_entry.adapter_handle);
    if (adapter_result.Ok()) {
        adapter_result.value->child_battery_handles.push_back(handle);
    }
    return Result<std::uint32_t>::Success(handle);
}

Result<std::uint32_t> RuntimeStore::GetImu(std::uint32_t bus_handle, int imu_idx) {
    auto bus_result = ResolveBus(bus_handle);
    if (!bus_result.Ok()) {
        return Result<std::uint32_t>::Failure(bus_result.code, bus_result.message);
    }
    auto& bus_entry = *bus_result.value;
    auto existing = bus_entry.imu_handles_by_index.find(imu_idx);
    if (existing != bus_entry.imu_handles_by_index.end()) {
        return Result<std::uint32_t>::Success(existing->second);
    }

    auto imu = bus_entry.bus->GetImu(imu_idx);
    if (!imu) {
        return Result<std::uint32_t>::Failure(ErrorCode::OperationFailed, "GetImu returned null");
    }

    ImuEntry entry;
    entry.adapter_handle = bus_entry.adapter_handle;
    entry.bus_handle = bus_handle;
    entry.imu_idx = imu_idx;
    entry.imu = imu;
    const auto handle = AllocateHandle();
    imus_[handle] = entry;
    bus_entry.imu_handles_by_index[imu_idx] = handle;
    auto adapter_result = ResolveAdapter(bus_entry.adapter_handle);
    if (adapter_result.Ok()) {
        adapter_result.value->child_imu_handles.push_back(handle);
    }
    return Result<std::uint32_t>::Success(handle);
}

Result<AdapterEntry*> RuntimeStore::ResolveAdapter(std::uint32_t adapter_handle) {
    auto it = adapters_.find(adapter_handle);
    if (it == adapters_.end() || !it->second.valid) {
        return Result<AdapterEntry*>::Failure(ErrorCode::InvalidHandle, "invalid adapter handle");
    }
    return Result<AdapterEntry*>::Success(&it->second);
}

Result<BusEntry*> RuntimeStore::ResolveBus(std::uint32_t bus_handle) {
    auto it = buses_.find(bus_handle);
    if (it == buses_.end() || !it->second.valid) {
        return Result<BusEntry*>::Failure(ErrorCode::InvalidHandle, "invalid bus handle");
    }
    auto adapter_result = ResolveAdapter(it->second.adapter_handle);
    if (!adapter_result.Ok()) {
        return Result<BusEntry*>::Failure(adapter_result.code, adapter_result.message);
    }
    return Result<BusEntry*>::Success(&it->second);
}

Result<MotorEntry*> RuntimeStore::ResolveMotor(std::uint32_t motor_handle) {
    auto it = motors_.find(motor_handle);
    if (it == motors_.end() || !it->second.valid) {
        return Result<MotorEntry*>::Failure(ErrorCode::InvalidHandle, "invalid motor handle");
    }
    auto bus_result = ResolveBus(it->second.bus_handle);
    if (!bus_result.Ok()) {
        return Result<MotorEntry*>::Failure(bus_result.code, bus_result.message);
    }
    return Result<MotorEntry*>::Success(&it->second);
}

Result<BatteryEntry*> RuntimeStore::ResolveBattery(std::uint32_t battery_handle) {
    auto it = batteries_.find(battery_handle);
    if (it == batteries_.end() || !it->second.valid) {
        return Result<BatteryEntry*>::Failure(ErrorCode::InvalidHandle, "invalid battery handle");
    }
    auto bus_result = ResolveBus(it->second.bus_handle);
    if (!bus_result.Ok()) {
        return Result<BatteryEntry*>::Failure(bus_result.code, bus_result.message);
    }
    return Result<BatteryEntry*>::Success(&it->second);
}

Result<ImuEntry*> RuntimeStore::ResolveImu(std::uint32_t imu_handle) {
    auto it = imus_.find(imu_handle);
    if (it == imus_.end() || !it->second.valid) {
        return Result<ImuEntry*>::Failure(ErrorCode::InvalidHandle, "invalid IMU handle");
    }
    auto bus_result = ResolveBus(it->second.bus_handle);
    if (!bus_result.Ok()) {
        return Result<ImuEntry*>::Failure(bus_result.code, bus_result.message);
    }
    return Result<ImuEntry*>::Success(&it->second);
}

Result<FakeAdapter*> RuntimeStore::ResolveFakeAdapter(std::uint32_t adapter_handle) {
    auto adapter_result = ResolveAdapter(adapter_handle);
    if (!adapter_result.Ok()) {
        return Result<FakeAdapter*>::Failure(adapter_result.code, adapter_result.message);
    }
    auto& entry = *adapter_result.value;
    if (!entry.fake_tools_enabled || !entry.fake_adapter) {
        return Result<FakeAdapter*>::Failure(ErrorCode::WrongAdapterType,
                                             "adapter was not created with createFakeAdapter");
    }
    return Result<FakeAdapter*>::Success(entry.fake_adapter);
}

ErrorCode RuntimeStore::SeedFakeMotor(std::uint32_t adapter_handle, int bus_idx, int motor_idx,
                                      MotorModel model) {
    auto fake_result = ResolveFakeAdapter(adapter_handle);
    if (!fake_result.Ok()) {
        return fake_result.code;
    }
    fake_result.value->SeedMotor(bus_idx, motor_idx, model);
    return ErrorCode::Ok;
}

ErrorCode RuntimeStore::EnableFakeAutoCreateMotor(std::uint32_t adapter_handle) {
    auto fake_result = ResolveFakeAdapter(adapter_handle);
    if (!fake_result.Ok()) {
        return fake_result.code;
    }
    fake_result.value->EnableAutoCreateMotor();
    return ErrorCode::Ok;
}

ErrorCode RuntimeStore::SetFakeReplyMode(std::uint32_t adapter_handle, FakeReplyMode mode) {
    auto fake_result = ResolveFakeAdapter(adapter_handle);
    if (!fake_result.Ok()) {
        return fake_result.code;
    }
    fake_result.value->SetReplyMode(mode);
    return ErrorCode::Ok;
}

ErrorCode RuntimeStore::InjectFakeFeedback(std::uint32_t adapter_handle, int bus_idx, int motor_idx,
                                           const MotorStatus& status, int feedback_type) {
    auto fake_result = ResolveFakeAdapter(adapter_handle);
    if (!fake_result.Ok()) {
        return fake_result.code;
    }
    auto* fake = fake_result.value;
    fake->InjectMessage(fake->MakeFeedbackMessage(bus_idx, motor_idx, status, feedback_type));
    return ErrorCode::Ok;
}

ErrorCode RuntimeStore::SetFakeParameterWritePolicy(std::uint32_t adapter_handle, int bus_idx,
                                                    int motor_idx, MotorParameter parameter,
                                                    FakeWritePolicy policy) {
    auto fake_result = ResolveFakeAdapter(adapter_handle);
    if (!fake_result.Ok()) {
        return fake_result.code;
    }
    fake_result.value->SetParameterWritePolicy(bus_idx, motor_idx, parameter, policy);
    return ErrorCode::Ok;
}

Result<int> RuntimeStore::FakeCommandCount(std::uint32_t adapter_handle) {
    auto fake_result = ResolveFakeAdapter(adapter_handle);
    if (!fake_result.Ok()) {
        return Result<int>::Failure(fake_result.code, fake_result.message);
    }
    return Result<int>::Success(static_cast<int>(fake_result.value->GetCommandRecords().size()));
}

ErrorCode RuntimeStore::InjectFakeRawMessage(std::uint32_t adapter_handle, int bus_idx,
                                             std::uint32_t can_id, std::uint8_t frame_flags,
                                             const std::uint8_t* data, int len) {
    auto fake_result = ResolveFakeAdapter(adapter_handle);
    if (!fake_result.Ok()) {
        return fake_result.code;
    }
    if (data == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    if (len < 0 || len > static_cast<int>(sizeof(MotorPackMsg::data))) {
        return ErrorCode::InvalidArgument;
    }

    MotorPackMsg pack{};
    pack.id = can_id;
    pack.frame_flags = frame_flags;
    pack.len = static_cast<std::uint8_t>(len);
    for (int i = 0; i < len; ++i) {
        pack.data[i] = data[i];
    }
    fake_result.value->InjectMessage(MotorMessage{bus_idx, pack});
    return ErrorCode::Ok;
}

Result<int> RuntimeStore::FakeRawMessageCount(std::uint32_t adapter_handle) {
    auto fake_result = ResolveFakeAdapter(adapter_handle);
    if (!fake_result.Ok()) {
        return Result<int>::Failure(fake_result.code, fake_result.message);
    }
    return Result<int>::Success(static_cast<int>(fake_result.value->GetRawSentMessages().size()));
}

Result<MotorMessage> RuntimeStore::FakeRawMessageAt(std::uint32_t adapter_handle,
                                                    int message_index) {
    auto fake_result = ResolveFakeAdapter(adapter_handle);
    if (!fake_result.Ok()) {
        return Result<MotorMessage>::Failure(fake_result.code, fake_result.message);
    }
    const auto messages = fake_result.value->GetRawSentMessages();
    if (message_index < 0 || static_cast<std::size_t>(message_index) >= messages.size()) {
        return Result<MotorMessage>::Failure(ErrorCode::InvalidArgument,
                                             "message_index is out of range");
    }
    return Result<MotorMessage>::Success(messages[static_cast<std::size_t>(message_index)]);
}

void RuntimeStore::SetLastError(ErrorCode code, std::string message) {
    last_error_.code = code;
    last_error_.message = std::move(message);
}

void RuntimeStore::ClearLastError() {
    last_error_ = {};
}

const ErrorState& RuntimeStore::LastError() const {
    return last_error_;
}

Result<std::uint32_t> RuntimeStore::AdoptBus(std::uint32_t parent, Bus* pointer) {
    auto adapter = ResolveAdapter(parent);
    if (!adapter.Ok() || !pointer)
        return Result<std::uint32_t>::Failure(ErrorCode::InvalidHandle, "invalid parent or bus");
    for (auto& [handle, entry] : buses_)
        if (entry.adapter_handle == parent && entry.bus == pointer)
            return Result<std::uint32_t>::Success(handle);
    auto handle = AllocateHandle();
    const int index = pointer->GetBusIndex();
    buses_[handle] = BusEntry{parent, pointer, BusKey{index, -1, index}, {}, {}, {}, true};
    adapter.value->child_bus_handles.push_back(handle);
    adapter.value->bus_handles_by_raw_index[index] = handle;
    return Result<std::uint32_t>::Success(handle);
}
Result<std::uint32_t> RuntimeStore::AdoptMotor(std::uint32_t parent, Motor* pointer) {
    auto bus = ResolveBus(parent);
    if (!bus.Ok() || !pointer)
        return Result<std::uint32_t>::Failure(ErrorCode::InvalidHandle, "invalid parent or motor");
    RefreshMotorIndices();
    for (auto& [handle, entry] : motors_)
        if (entry.bus_handle == parent && entry.motor == pointer)
            return Result<std::uint32_t>::Success(handle);
    auto handle = AllocateHandle();
    std::optional<int> index;
    for (const auto& item : EncosDriverManager::Instance().GetMotors(bus.value->bus))
        if (item.second == pointer)
            index = item.first;
    if (!index)
        return Result<std::uint32_t>::Failure(ErrorCode::InvalidHandle, "motor is not registered");
    motors_[handle] = MotorEntry{bus.value->adapter_handle, parent, *index, pointer, true};
    bus.value->motor_handles_by_index[*index] = handle;
    adapters_.at(bus.value->adapter_handle).child_motor_handles.push_back(handle);
    return Result<std::uint32_t>::Success(handle);
}
void RuntimeStore::RefreshMotorIndices() {
    for (auto& [handle, bus] : buses_) {
        (void) handle;
        bus.motor_handles_by_index.clear();
    }
    for (auto it = motors_.begin(); it != motors_.end();) {
        auto& motor = it->second;
        auto bus = ResolveBus(motor.bus_handle);
        std::optional<int> index;
        if (bus.Ok())
            for (const auto& item : EncosDriverManager::Instance().GetMotors(bus.value->bus))
                if (item.second == motor.motor)
                    index = item.first;
        if (!index) {
            it = motors_.erase(it);
            continue;
        }
        motor.motor_idx = *index;
        bus.value->motor_handles_by_index[*index] = it->first;
        ++it;
    }
}
Result<std::uint32_t> RuntimeStore::AdoptBattery(std::uint32_t parent, Battery* pointer,
                                                 int index) {
    auto bus = ResolveBus(parent);
    if (!bus.Ok() || !pointer)
        return Result<std::uint32_t>::Failure(ErrorCode::InvalidHandle,
                                              "invalid parent or battery");
    for (auto& [handle, entry] : batteries_)
        if (entry.bus_handle == parent && entry.battery == pointer)
            return Result<std::uint32_t>::Success(handle);
    auto handle = AllocateHandle();
    batteries_[handle] = BatteryEntry{bus.value->adapter_handle, parent, index, pointer, true};
    bus.value->battery_handles_by_index[index] = handle;
    adapters_.at(bus.value->adapter_handle).child_battery_handles.push_back(handle);
    return Result<std::uint32_t>::Success(handle);
}
Result<std::uint32_t> RuntimeStore::AdoptImu(std::uint32_t parent, Imu* pointer, int index) {
    auto bus = ResolveBus(parent);
    if (!bus.Ok() || !pointer)
        return Result<std::uint32_t>::Failure(ErrorCode::InvalidHandle, "invalid parent or imu");
    for (auto& [handle, entry] : imus_)
        if (entry.bus_handle == parent && entry.imu == pointer)
            return Result<std::uint32_t>::Success(handle);
    auto handle = AllocateHandle();
    imus_[handle] = ImuEntry{bus.value->adapter_handle, parent, index, pointer, true};
    bus.value->imu_handles_by_index[index] = handle;
    adapters_.at(bus.value->adapter_handle).child_imu_handles.push_back(handle);
    return Result<std::uint32_t>::Success(handle);
}
Result<std::uint32_t> RuntimeStore::AdoptPms(std::uint32_t parent, Pms* pointer) {
    auto bus = ResolveBus(parent);
    if (!bus.Ok() || !pointer)
        return Result<std::uint32_t>::Failure(ErrorCode::InvalidHandle, "invalid parent or pms");
    for (auto& [handle, entry] : pmss_)
        if (entry.bus_handle == parent && entry.pms == pointer)
            return Result<std::uint32_t>::Success(handle);
    auto handle = AllocateHandle();
    pmss_[handle] = PmsEntry{bus.value->adapter_handle, parent, pointer};
    return Result<std::uint32_t>::Success(handle);
}
Result<std::uint32_t> RuntimeStore::AdoptDexHand(std::uint32_t parent, DexHand* hand) {
    auto bus = ResolveBus(parent);
    if (!bus.Ok() || !hand) {
        return Result<std::uint32_t>::Failure(ErrorCode::InvalidHandle,
                                              "invalid parent or DexHand");
    }
    auto& handles = bus.value->dex_hand_handles;
    for (const auto& [handle, pointer] : handles) {
        if (pointer == hand) {
            return Result<std::uint32_t>::Success(handle);
        }
    }
    const auto handle = AllocateHandle();
    handles.emplace(handle, hand);
    return Result<std::uint32_t>::Success(handle);
}
Result<std::uint32_t> RuntimeStore::AdoptGlove(std::uint32_t parent, Glove* pointer,
                                               int slave_index) {
    if (!ResolveAdapter(parent).Ok() || !pointer)
        return Result<std::uint32_t>::Failure(ErrorCode::InvalidHandle, "invalid parent or glove");
    for (auto& [handle, entry] : gloves_)
        if (entry.adapter_handle == parent && entry.glove == pointer)
            return Result<std::uint32_t>::Success(handle);
    auto handle = AllocateHandle();
    gloves_[handle] = GloveEntry{parent, pointer, slave_index};
    return Result<std::uint32_t>::Success(handle);
}
Result<PmsEntry*> RuntimeStore::ResolvePms(std::uint32_t handle) {
    auto it = pmss_.find(handle);
    if (it == pmss_.end() || !ResolveBus(it->second.bus_handle).Ok())
        return Result<PmsEntry*>::Failure(ErrorCode::InvalidHandle, "invalid pms handle");
    return Result<PmsEntry*>::Success(&it->second);
}
Result<DexHand*> RuntimeStore::ResolveDexHand(std::uint32_t handle) {
    for (const auto& entry : buses_) {
        const auto& handles = entry.second.dex_hand_handles;
        if (auto it = handles.find(handle); it != handles.end()) {
            return Result<DexHand*>::Success(it->second);
        }
    }
    return Result<DexHand*>::Failure(ErrorCode::InvalidHandle, "invalid DexHand handle");
}
Result<GloveEntry*> RuntimeStore::ResolveGlove(std::uint32_t handle) {
    auto it = gloves_.find(handle);
    if (it == gloves_.end() || !ResolveAdapter(it->second.adapter_handle).Ok())
        return Result<GloveEntry*>::Failure(ErrorCode::InvalidHandle, "invalid glove handle");
    return Result<GloveEntry*>::Success(&it->second);
}
ErrorCode RuntimeStore::DisposeMotor(std::uint32_t handle) {
    auto resolved = ResolveMotor(handle);
    if (!resolved.Ok())
        return resolved.code;
    auto* pointer = resolved.value->motor;
    if (!EncosDriverManager::Instance().DestroyMotor(pointer))
        return ErrorCode::OperationFailed;
    for (auto it = motors_.begin(); it != motors_.end();) {
        if (it->second.motor == pointer)
            it = motors_.erase(it);
        else
            ++it;
    }
    for (auto& [bus_handle, bus] : buses_) {
        (void) bus_handle;
        for (auto it = bus.motor_handles_by_index.begin();
             it != bus.motor_handles_by_index.end();) {
            if (motors_.count(it->second) == 0)
                it = bus.motor_handles_by_index.erase(it);
            else
                ++it;
        }
    }
    return ErrorCode::Ok;
}
ErrorCode RuntimeStore::DisposeBattery(std::uint32_t handle) {
    auto resolved = ResolveBattery(handle);
    if (!resolved.Ok())
        return resolved.code;
    auto* pointer = resolved.value->battery;
    if (!EncosDriverManager::Instance().DestroyBattery(pointer))
        return ErrorCode::OperationFailed;
    for (auto it = batteries_.begin(); it != batteries_.end();) {
        if (it->second.battery == pointer)
            it = batteries_.erase(it);
        else
            ++it;
    }
    for (auto& [bus_handle, bus] : buses_) {
        (void) bus_handle;
        for (auto it = bus.battery_handles_by_index.begin();
             it != bus.battery_handles_by_index.end();) {
            if (batteries_.count(it->second) == 0)
                it = bus.battery_handles_by_index.erase(it);
            else
                ++it;
        }
    }
    return ErrorCode::Ok;
}
ErrorCode RuntimeStore::DisposeImu(std::uint32_t handle) {
    auto resolved = ResolveImu(handle);
    if (!resolved.Ok())
        return resolved.code;
    auto* pointer = resolved.value->imu;
    if (!EncosDriverManager::Instance().DestroyImu(pointer))
        return ErrorCode::OperationFailed;
    for (auto it = imus_.begin(); it != imus_.end();) {
        if (it->second.imu == pointer)
            it = imus_.erase(it);
        else
            ++it;
    }
    for (auto& [bus_handle, bus] : buses_) {
        (void) bus_handle;
        for (auto it = bus.imu_handles_by_index.begin(); it != bus.imu_handles_by_index.end();) {
            if (imus_.count(it->second) == 0)
                it = bus.imu_handles_by_index.erase(it);
            else
                ++it;
        }
    }
    return ErrorCode::Ok;
}
ErrorCode RuntimeStore::DisposePms(std::uint32_t handle) {
    auto resolved = ResolvePms(handle);
    if (!resolved.Ok())
        return resolved.code;
    auto* pointer = resolved.value->pms;
    if (!EncosDriverManager::Instance().DestroyPms(pointer))
        return ErrorCode::OperationFailed;
    for (auto it = pmss_.begin(); it != pmss_.end();) {
        if (it->second.pms == pointer)
            it = pmss_.erase(it);
        else
            ++it;
    }
    return ErrorCode::Ok;
}
ErrorCode RuntimeStore::DisposeDexHand(std::uint32_t handle) {
    auto resolved = ResolveDexHand(handle);
    if (!resolved.Ok()) {
        return resolved.code;
    }
    auto* hand = resolved.value;
    if (!EncosDriverManager::Instance().DestroyDexHand(hand)) {
        return ErrorCode::OperationFailed;
    }
    for (auto& entry : buses_) {
        auto& handles = entry.second.dex_hand_handles;
        for (auto it = handles.begin(); it != handles.end();) {
            if (it->second == hand) {
                it = handles.erase(it);
            } else {
                ++it;
            }
        }
    }
    return ErrorCode::Ok;
}
ErrorCode RuntimeStore::DisposeGlove(std::uint32_t handle) {
    auto resolved = ResolveGlove(handle);
    if (!resolved.Ok())
        return resolved.code;
    auto* pointer = resolved.value->glove;
    if (!EncosDriverManager::Instance().DestroyGlove(pointer))
        return ErrorCode::OperationFailed;
    for (auto it = gloves_.begin(); it != gloves_.end();) {
        if (it->second.glove == pointer)
            it = gloves_.erase(it);
        else
            ++it;
    }
    std::unordered_set<std::uint32_t> removed_buses;
    for (auto it = buses_.begin(); it != buses_.end();) {
        auto parent = ResolveAdapter(it->second.adapter_handle);
        bool alive = false;
        if (parent.Ok())
            for (const auto& bus : EncosDriverManager::Instance().GetBuses(parent.value->adapter))
                if (bus.second == it->second.bus)
                    alive = true;
        if (!alive) {
            removed_buses.insert(it->first);
            it = buses_.erase(it);
        } else
            ++it;
    }
    for (auto it = motors_.begin(); it != motors_.end();) {
        if (removed_buses.count(it->second.bus_handle))
            it = motors_.erase(it);
        else
            ++it;
    }
    for (auto it = batteries_.begin(); it != batteries_.end();) {
        if (removed_buses.count(it->second.bus_handle))
            it = batteries_.erase(it);
        else
            ++it;
    }
    for (auto it = imus_.begin(); it != imus_.end();) {
        if (removed_buses.count(it->second.bus_handle))
            it = imus_.erase(it);
        else
            ++it;
    }
    for (auto it = pmss_.begin(); it != pmss_.end();) {
        if (removed_buses.count(it->second.bus_handle))
            it = pmss_.erase(it);
        else
            ++it;
    }
    for (auto& adapter : adapters_)
        for (auto it = adapter.second.bus_handles_by_raw_index.begin();
             it != adapter.second.bus_handles_by_raw_index.end();) {
            if (removed_buses.count(it->second))
                it = adapter.second.bus_handles_by_raw_index.erase(it);
            else
                ++it;
        }
    return ErrorCode::Ok;
}
ErrorCode RuntimeStore::DisposeBus(std::uint32_t handle) {
    auto resolved = ResolveBus(handle);
    if (!resolved.Ok())
        return resolved.code;
    auto* pointer = resolved.value->bus;
    if (!EncosDriverManager::Instance().DestroyBus(pointer))
        return ErrorCode::OperationFailed;
    std::unordered_set<std::uint32_t> deleted;
    for (auto it = buses_.begin(); it != buses_.end();) {
        if (it->second.bus == pointer) {
            deleted.insert(it->first);
            it = buses_.erase(it);
        } else
            ++it;
    }
    for (auto it = motors_.begin(); it != motors_.end();) {
        if (deleted.count(it->second.bus_handle))
            it = motors_.erase(it);
        else
            ++it;
    }
    for (auto it = batteries_.begin(); it != batteries_.end();) {
        if (deleted.count(it->second.bus_handle))
            it = batteries_.erase(it);
        else
            ++it;
    }
    for (auto it = imus_.begin(); it != imus_.end();) {
        if (deleted.count(it->second.bus_handle))
            it = imus_.erase(it);
        else
            ++it;
    }
    for (auto it = pmss_.begin(); it != pmss_.end();) {
        if (deleted.count(it->second.bus_handle))
            it = pmss_.erase(it);
        else
            ++it;
    }
    for (auto& [adapter_handle, adapter] : adapters_) {
        (void) adapter_handle;
        for (auto it = adapter.bus_handles_by_raw_index.begin();
             it != adapter.bus_handles_by_raw_index.end();) {
            if (deleted.count(it->second))
                it = adapter.bus_handles_by_raw_index.erase(it);
            else
                ++it;
        }
    }
    return ErrorCode::Ok;
}
bool RuntimeStore::IsHandleAlive(std::uint32_t handle) {
    return ResolveAdapter(handle).Ok() || ResolveBus(handle).Ok() || ResolveMotor(handle).Ok() ||
           ResolveBattery(handle).Ok() || ResolveImu(handle).Ok() || ResolvePms(handle).Ok() ||
           ResolveGlove(handle).Ok();
}
bool RuntimeStore::IsHandleDescendant(std::uint32_t source, std::uint32_t target) const {
    if (source == target)
        return true;
    std::uint32_t adapter = 0, bus = 0;
    if (auto it = buses_.find(source); it != buses_.end()) {
        adapter = it->second.adapter_handle;
        bus = source;
    }
    if (auto it = motors_.find(source); it != motors_.end()) {
        adapter = it->second.adapter_handle;
        bus = it->second.bus_handle;
    }
    if (auto it = batteries_.find(source); it != batteries_.end()) {
        adapter = it->second.adapter_handle;
        bus = it->second.bus_handle;
    }
    if (auto it = imus_.find(source); it != imus_.end()) {
        adapter = it->second.adapter_handle;
        bus = it->second.bus_handle;
    }
    if (auto it = pmss_.find(source); it != pmss_.end()) {
        adapter = it->second.adapter_handle;
        bus = it->second.bus_handle;
    }
    if (auto it = gloves_.find(source); it != gloves_.end())
        adapter = it->second.adapter_handle;
    if (adapter == target)
        return true;
    auto target_bus = buses_.find(target);
    auto source_bus = buses_.find(bus);
    auto target_glove = gloves_.find(target);
    if (target_glove != gloves_.end() && source_bus != buses_.end()) {
        const auto glove_adapter = adapters_.find(target_glove->second.adapter_handle);
        const auto source_adapter = adapters_.find(source_bus->second.adapter_handle);
        if (glove_adapter != adapters_.end() && source_adapter != adapters_.end() &&
            glove_adapter->second.adapter == source_adapter->second.adapter) {
            for (int finger = 0; finger < 5; ++finger)
                if (source_bus->second.key.raw_idx ==
                    target_glove->second.slave_index * 65536 + finger)
                    return true;
        }
    }
    if (target_bus != buses_.end() && source_bus != buses_.end() &&
        target_bus->second.bus == source_bus->second.bus)
        return true;
    if (auto left = motors_.find(source); left != motors_.end()) {
        auto right = motors_.find(target);
        if (right != motors_.end() && left->second.motor == right->second.motor)
            return true;
    }
    if (auto left = batteries_.find(source); left != batteries_.end()) {
        auto right = batteries_.find(target);
        if (right != batteries_.end() && left->second.battery == right->second.battery)
            return true;
    }
    if (auto left = imus_.find(source); left != imus_.end()) {
        auto right = imus_.find(target);
        if (right != imus_.end() && left->second.imu == right->second.imu)
            return true;
    }
    if (auto left = pmss_.find(source); left != pmss_.end()) {
        auto right = pmss_.find(target);
        if (right != pmss_.end() && left->second.pms == right->second.pms)
            return true;
    }
    if (auto left = gloves_.find(source); left != gloves_.end()) {
        auto right = gloves_.find(target);
        if (right != gloves_.end() && left->second.glove == right->second.glove)
            return true;
    }
    return false;
}
}  // namespace encos::wasm
