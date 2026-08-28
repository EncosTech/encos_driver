#include "wasm/wasm_runtime.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace encos::wasm {

namespace {

std::string DefaultLoggerName(const std::string& adapter_type, const std::string& logger_name) {
    if (!logger_name.empty()) {
        return logger_name;
    }
    return adapter_type + "Adapter";
}

}  // namespace

RuntimeStore& Store() {
    static RuntimeStore store;
    return store;
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
                                                  LogLevel log_level) {
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
                                                               logger_name, log_level);
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
    if (len < 0 || len > 8) {
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

}  // namespace encos::wasm
