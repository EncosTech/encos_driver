#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "battery/battery.h"
#include "bus/bus.h"
#include "encos_motor.h"
#include "imu/imu.h"
#include "motor/motor.h"
#include "plugins/fake/fake_adapter.h"

namespace encos::wasm {

struct RuntimeStoreTestAccess;

enum class ErrorCode : int {
    Ok = 0,
    InvalidArgument = 1,
    InvalidHandle = 2,
    Disposed = 3,
    WrongAdapterType = 4,
    OperationFailed = 5,
    NoResponse = 6,
    Unsupported = 7,
    ResourceExhausted = 8,
    InternalError = 9,
};

struct ErrorState {
    ErrorCode code = ErrorCode::Ok;
    std::string message;
};

template <typename T>
struct Result {
    ErrorCode code = ErrorCode::Ok;
    std::string message;
    T value{};

    static Result Success(T result_value) {
        Result result;
        result.value = std::move(result_value);
        return result;
    }

    static Result Failure(ErrorCode error_code, std::string error_message) {
        Result result;
        result.code = error_code;
        result.message = std::move(error_message);
        return result;
    }

    bool Ok() const {
        return code == ErrorCode::Ok;
    }
};

struct BusKey {
    int raw_idx = 0;
    int slave_idx = -1;
    int bus_idx = 0;
};

struct AdapterEntry {
    BaseAdapter* adapter = nullptr;
    FakeAdapter* fake_adapter = nullptr;
    std::string interface_name;
    std::unordered_map<int, std::uint32_t> bus_handles_by_raw_index;
    std::vector<std::uint32_t> child_bus_handles;
    std::vector<std::uint32_t> child_motor_handles;
    std::vector<std::uint32_t> child_battery_handles;
    std::vector<std::uint32_t> child_imu_handles;
    bool fake_tools_enabled = false;
    bool disposing = false;
    bool valid = true;
};

struct BusEntry {
    std::uint32_t adapter_handle = 0;
    Bus* bus = nullptr;
    BusKey key;
    std::unordered_map<int, std::uint32_t> motor_handles_by_index;
    std::unordered_map<int, std::uint32_t> battery_handles_by_index;
    std::unordered_map<int, std::uint32_t> imu_handles_by_index;
    bool valid = true;
};

struct MotorEntry {
    std::uint32_t adapter_handle = 0;
    std::uint32_t bus_handle = 0;
    int motor_idx = -1;
    Motor* motor = nullptr;
    bool valid = true;
};

struct BatteryEntry {
    std::uint32_t adapter_handle = 0;
    std::uint32_t bus_handle = 0;
    int battery_idx = -1;
    Battery* battery = nullptr;
    bool valid = true;
};

struct ImuEntry {
    std::uint32_t adapter_handle = 0;
    std::uint32_t bus_handle = 0;
    int imu_idx = -1;
    Imu* imu = nullptr;
    bool valid = true;
};

class RuntimeStore {
    friend struct RuntimeStoreTestAccess;

public:
    Result<std::uint32_t> CreateAdapter(const std::string& adapter_type,
                                        const std::string& interface_name,
                                        const std::string& logger_name, LogLevel log_level);
    Result<std::uint32_t> CreateFakeAdapter(const std::string& interface_name,
                                            const std::string& logger_name, LogLevel log_level);
    ErrorCode DisposeAdapter(std::uint32_t adapter_handle);

    Result<std::uint32_t> GetBus(std::uint32_t adapter_handle, int slave_idx, int bus_idx);
    Result<std::vector<int>> ListBusRawIndices(std::uint32_t adapter_handle);
    Result<std::uint32_t> GetMotorWithModel(std::uint32_t bus_handle, int motor_idx,
                                            MotorModel model);
    Result<std::uint32_t> GetBattery(std::uint32_t bus_handle, int battery_idx);
    Result<std::uint32_t> GetImu(std::uint32_t bus_handle, int imu_idx);

    Result<AdapterEntry*> ResolveAdapter(std::uint32_t adapter_handle);
    Result<BusEntry*> ResolveBus(std::uint32_t bus_handle);
    Result<MotorEntry*> ResolveMotor(std::uint32_t motor_handle);
    Result<BatteryEntry*> ResolveBattery(std::uint32_t battery_handle);
    Result<ImuEntry*> ResolveImu(std::uint32_t imu_handle);
    Result<FakeAdapter*> ResolveFakeAdapter(std::uint32_t adapter_handle);

    ErrorCode SeedFakeMotor(std::uint32_t adapter_handle, int bus_idx, int motor_idx,
                            MotorModel model);
    ErrorCode EnableFakeAutoCreateMotor(std::uint32_t adapter_handle);
    ErrorCode SetFakeReplyMode(std::uint32_t adapter_handle, FakeReplyMode mode);
    ErrorCode InjectFakeFeedback(std::uint32_t adapter_handle, int bus_idx, int motor_idx,
                                 const MotorStatus& status, int feedback_type);
    ErrorCode SetFakeParameterWritePolicy(std::uint32_t adapter_handle, int bus_idx, int motor_idx,
                                          MotorParameter parameter, FakeWritePolicy policy);
    Result<int> FakeCommandCount(std::uint32_t adapter_handle);
    ErrorCode InjectFakeRawMessage(std::uint32_t adapter_handle, int bus_idx, std::uint32_t can_id,
                                   std::uint8_t frame_flags, const std::uint8_t* data, int len);
    Result<int> FakeRawMessageCount(std::uint32_t adapter_handle);
    Result<MotorMessage> FakeRawMessageAt(std::uint32_t adapter_handle, int message_index);

    void SetLastError(ErrorCode code, std::string message);
    void ClearLastError();
    const ErrorState& LastError() const;

private:
    struct PendingAdapterDeletion {
        std::uint32_t adapter_handle = 0;
        std::string interface_name;
        BaseAdapter* adapter = nullptr;
        std::vector<Motor*> motors;
        std::uint32_t retry_count = 0;
        int retry_delay_ms = 1;
        bool exhausted = false;
    };

    bool BeginPendingAdapterDeletion(std::uint32_t adapter_handle, std::vector<Motor*> motors);
    int PollPendingAdapterDeletionsOnce();
#ifdef __EMSCRIPTEN__
    static void PollPendingAdapterDeletionsCallback(void* context);
    void PollPendingAdapterDeletions();
#endif
    std::uint32_t AllocateHandle();

    std::uint32_t next_handle_ = 1;
    std::unordered_map<std::uint32_t, AdapterEntry> adapters_;
    std::unordered_map<std::uint32_t, BusEntry> buses_;
    std::unordered_map<std::uint32_t, MotorEntry> motors_;
    std::unordered_map<std::uint32_t, BatteryEntry> batteries_;
    std::unordered_map<std::uint32_t, ImuEntry> imus_;
    std::unordered_map<BaseAdapter*, std::size_t> adapter_handle_leases_;
    std::unordered_map<BaseAdapter*, PendingAdapterDeletion> pending_adapter_deletions_;
    std::unordered_set<std::string> pending_adapter_interfaces_;
    LoggerPtr logger_ = CreateLogger("WasmRuntimeStore");
    ErrorState last_error_;
};

RuntimeStore& Store();
ErrorCode SetResultError(ErrorCode code, const std::string& message);
void SetOk();

}  // namespace encos::wasm
