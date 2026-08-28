#include <cstddef>
#include <cstdint>
#include <emscripten/emscripten.h>
#include <exception>
#include <limits>
#include <string>
#include <variant>

#include "encos_version.h"
#include "wasm/wasm_runtime.h"

namespace {

using encos::wasm::ErrorCode;

int ToInt(ErrorCode code) {
    return static_cast<int>(code);
}

ErrorCode CatchException(const char* function_name) {
    try {
        throw;
    } catch (const std::exception& e) {
        return encos::wasm::SetResultError(ErrorCode::InternalError,
                                           std::string(function_name) + ": " + e.what());
    } catch (...) {
        return encos::wasm::SetResultError(ErrorCode::InternalError,
                                           std::string(function_name) + ": unknown exception");
    }
}

ErrorCode RequirePointer(const void* ptr, const char* name) {
    if (ptr == nullptr) {
        return encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                           std::string(name) + " must not be null");
    }
    return ErrorCode::Ok;
}

ErrorCode RequireControlBuffers(double* out_values, int values_len, std::int32_t* out_meta,
                                int meta_len) {
    if (RequirePointer(out_values, "out_values") != ErrorCode::Ok ||
        RequirePointer(out_meta, "out_meta") != ErrorCode::Ok) {
        return ErrorCode::InvalidArgument;
    }
    if (values_len < 5 || meta_len < 5) {
        return encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                           "control buffers are too small");
    }
    return ErrorCode::Ok;
}

void FillEmptyControlResult(double* out_values, std::int32_t* out_meta, int feedback_type) {
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    for (int i = 0; i < 5; ++i) {
        out_values[i] = kNaN;
    }
    out_meta[0] = 1;
    out_meta[1] = ToInt(ErrorCode::Ok);
    out_meta[2] = feedback_type;
    out_meta[3] = static_cast<int>(encos::MotorError::NoError);
    out_meta[4] = 0;
}

void FillFeedback1(const encos::MotorFeedbackMsg1& feedback, double* out_values,
                   std::int32_t* out_meta, int feedback_type) {
    out_values[0] = feedback.position;
    out_values[1] = feedback.speed;
    out_values[2] = feedback.current;
    out_values[3] = feedback.motor_temperature;
    out_values[4] = feedback.mos_temperature;
    out_meta[0] = 1;
    out_meta[1] = ToInt(ErrorCode::Ok);
    out_meta[2] = feedback_type;
    out_meta[3] = static_cast<int>(feedback.error);
    out_meta[4] = 1;
}

void FillFeedback2(const encos::MotorFeedbackMsg2& feedback, double* out_values,
                   std::int32_t* out_meta, int feedback_type) {
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    out_values[0] = feedback.position;
    out_values[1] = kNaN;
    out_values[2] = feedback.current;
    out_values[3] = feedback.motor_temperature;
    out_values[4] = kNaN;
    out_meta[0] = 1;
    out_meta[1] = ToInt(ErrorCode::Ok);
    out_meta[2] = feedback_type;
    out_meta[3] = static_cast<int>(feedback.error);
    out_meta[4] = 1;
}

void FillFeedback3(const encos::MotorFeedbackMsg3& feedback, double* out_values,
                   std::int32_t* out_meta, int feedback_type) {
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    out_values[0] = kNaN;
    out_values[1] = feedback.speed;
    out_values[2] = feedback.current;
    out_values[3] = feedback.motor_temperature;
    out_values[4] = kNaN;
    out_meta[0] = 1;
    out_meta[1] = ToInt(ErrorCode::Ok);
    out_meta[2] = feedback_type;
    out_meta[3] = static_cast<int>(feedback.error);
    out_meta[4] = 1;
}

template <typename PayloadT>
const PayloadT* PayloadAs(const encos::FakeCommandRecord& record) {
    return std::get_if<PayloadT>(&record.payload);
}

encos::wasm::Result<int> GetFakeCommandCount(std::uint32_t adapter_handle) {
    return encos::wasm::Store().FakeCommandCount(adapter_handle);
}

encos::wasm::Result<encos::FakeCommandRecord> GetFakeCommand(std::uint32_t adapter_handle,
                                                             int command_index) {
    auto fake_result = encos::wasm::Store().ResolveFakeAdapter(adapter_handle);
    if (!fake_result.Ok()) {
        return encos::wasm::Result<encos::FakeCommandRecord>::Failure(fake_result.code,
                                                                      fake_result.message);
    }
    const auto records = fake_result.value->GetCommandRecords();
    if (command_index < 0 || static_cast<std::size_t>(command_index) >= records.size()) {
        return encos::wasm::Result<encos::FakeCommandRecord>::Failure(
            ErrorCode::InvalidArgument, "command_index is out of range");
    }
    return encos::wasm::Result<encos::FakeCommandRecord>::Success(records[command_index]);
}

encos::wasm::Result<int> GetFakeRawMessageCount(std::uint32_t adapter_handle) {
    return encos::wasm::Store().FakeRawMessageCount(adapter_handle);
}

encos::wasm::Result<encos::MotorMessage> GetFakeRawMessage(std::uint32_t adapter_handle,
                                                           int message_index) {
    return encos::wasm::Store().FakeRawMessageAt(adapter_handle, message_index);
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
int encos_get_last_error_code() {
    return ToInt(encos::wasm::Store().LastError().code);
}

EMSCRIPTEN_KEEPALIVE
const char* encos_get_last_error_message() {
    return encos::wasm::Store().LastError().message.c_str();
}

EMSCRIPTEN_KEEPALIVE
void encos_clear_last_error() {
    encos::wasm::Store().ClearLastError();
}

EMSCRIPTEN_KEEPALIVE
const char* encos_get_version() {
    return ENCOS_PROJECT_VERSION;
}

EMSCRIPTEN_KEEPALIVE
int encos_create_adapter(const char* adapter_type, const char* interface_name,
                         const char* logger_name, int log_level,
                         std::uint32_t* out_adapter_handle) {
    try {
        auto code = RequirePointer(out_adapter_handle, "out_adapter_handle");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().CreateAdapter(
            adapter_type == nullptr ? "" : adapter_type,
            interface_name == nullptr ? "" : interface_name,
            logger_name == nullptr ? "" : logger_name, encos::LogLevelFromInt(log_level));
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_adapter_handle = result.value;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_create_adapter"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_create_fake_adapter(const char* interface_name, const char* logger_name, int log_level,
                              std::uint32_t* out_adapter_handle) {
    try {
        auto code = RequirePointer(out_adapter_handle, "out_adapter_handle");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().CreateFakeAdapter(
            interface_name == nullptr ? "" : interface_name,
            logger_name == nullptr ? "" : logger_name, encos::LogLevelFromInt(log_level));
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_adapter_handle = result.value;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_create_fake_adapter"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_dispose_adapter(std::uint32_t adapter_handle) {
    try {
        auto code = encos::wasm::Store().DisposeAdapter(adapter_handle);
        if (code != ErrorCode::Ok) {
            encos::wasm::Store().SetLastError(code, "failed to dispose adapter");
            return ToInt(code);
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_dispose_adapter"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_adapter_ok(std::uint32_t adapter_handle, int* out_ok) {
    try {
        auto code = RequirePointer(out_ok, "out_ok");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().ResolveAdapter(adapter_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_ok = result.value->adapter->Ok() ? 1 : 0;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_adapter_ok"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_adapter_get_bus(std::uint32_t adapter_handle, int slave_idx, int bus_idx,
                          std::uint32_t* out_bus_handle) {
    try {
        auto code = RequirePointer(out_bus_handle, "out_bus_handle");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().GetBus(adapter_handle, slave_idx, bus_idx);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_bus_handle = result.value;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_adapter_get_bus"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_adapter_get_bus_count(std::uint32_t adapter_handle, int* out_count) {
    try {
        auto code = RequirePointer(out_count, "out_count");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().ListBusRawIndices(adapter_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_count = static_cast<int>(result.value.size());
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_adapter_get_bus_count"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_adapter_get_bus_raw_index_at(std::uint32_t adapter_handle, int index, int* out_raw_idx) {
    try {
        auto code = RequirePointer(out_raw_idx, "out_raw_idx");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().ListBusRawIndices(adapter_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        if (index < 0 || index >= static_cast<int>(result.value.size())) {
            return ToInt(
                encos::wasm::SetResultError(ErrorCode::InvalidArgument, "bus index out of range"));
        }
        *out_raw_idx = result.value[static_cast<std::size_t>(index)];
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_adapter_get_bus_raw_index_at"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_bus_get_index(std::uint32_t bus_handle, int* out_bus_idx) {
    try {
        auto code = RequirePointer(out_bus_idx, "out_bus_idx");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().ResolveBus(bus_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_bus_idx = result.value->key.raw_idx;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_bus_get_index"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_bus_get_motor_with_model(std::uint32_t bus_handle, int motor_idx, int model,
                                   std::uint32_t* out_motor_handle) {
    try {
        auto code = RequirePointer(out_motor_handle, "out_motor_handle");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().GetMotorWithModel(bus_handle, motor_idx,
                                                             static_cast<encos::MotorModel>(model));
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_motor_handle = result.value;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_bus_get_motor_with_model"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_bus_get_battery(std::uint32_t bus_handle, int battery_idx,
                          std::uint32_t* out_battery_handle) {
    try {
        auto code = RequirePointer(out_battery_handle, "out_battery_handle");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().GetBattery(bus_handle, battery_idx);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_battery_handle = result.value;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_bus_get_battery"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_bus_get_imu(std::uint32_t bus_handle, int imu_idx, std::uint32_t* out_imu_handle) {
    try {
        auto code = RequirePointer(out_imu_handle, "out_imu_handle");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().GetImu(bus_handle, imu_idx);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_imu_handle = result.value;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_bus_get_imu"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_motor_get_index(std::uint32_t motor_handle, int* out_motor_idx) {
    try {
        auto code = RequirePointer(out_motor_idx, "out_motor_idx");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().ResolveMotor(motor_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_motor_idx = result.value->motor_idx;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_motor_get_index"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_battery_get_status(std::uint32_t battery_handle, double* out_values, int values_len,
                             std::int32_t* out_meta, int meta_len) {
    try {
        if (RequirePointer(out_values, "out_values") != ErrorCode::Ok ||
            RequirePointer(out_meta, "out_meta") != ErrorCode::Ok) {
            return ToInt(ErrorCode::InvalidArgument);
        }
        if (values_len < 27 || meta_len < 3) {
            return ToInt(encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                                     "battery status buffers are too small"));
        }
        auto result = encos::wasm::Store().ResolveBattery(battery_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        const auto status = result.value->battery->GetStatus();
        for (int i = 0; i < 27; ++i) {
            out_values[i] = 0.0;
        }
        out_meta[0] = status.state.has_value() ? 1 : 0;
        out_meta[1] = status.temp.has_value() ? 1 : 0;
        out_meta[2] = status.active_commands.has_value() ? 1 : 0;
        if (status.state.has_value()) {
            out_values[0] = status.state->is_master ? 1.0 : 0.0;
            out_values[1] = status.state->soc;
            out_values[2] = status.state->voltage;
            out_values[3] = status.state->allowed_discharge_current;
            out_values[4] = status.state->allowed_charge_current;
        }
        if (status.temp.has_value()) {
            out_values[5] = status.temp->battery;
            out_values[6] = status.temp->mos;
            out_values[7] = status.temp->discharge_current;
            out_values[8] = status.temp->charge_current;
        }
        out_values[9] = status.error.could_not_charge ? 1.0 : 0.0;
        out_values[10] = status.error.could_not_discharge ? 1.0 : 0.0;
        out_values[11] = status.error.low_battery ? 1.0 : 0.0;
        out_values[12] = status.error.over_current_steady ? 1.0 : 0.0;
        out_values[13] = status.error.over_current_peak ? 1.0 : 0.0;
        out_values[14] = status.error.over_current_charge ? 1.0 : 0.0;
        out_values[15] = status.error.battery_over_temp ? 1.0 : 0.0;
        out_values[16] = status.error.mos_over_temp ? 1.0 : 0.0;
        out_values[17] = status.error.could_not_communicate ? 1.0 : 0.0;
        out_values[18] = status.error.stopped_emergency ? 1.0 : 0.0;
        out_values[19] = status.error.charger_fault ? 1.0 : 0.0;
        out_values[20] = status.error.comm_timeout ? 1.0 : 0.0;
        if (status.active_commands.has_value()) {
            out_values[21] = status.active_commands->shutdown_request ? 1.0 : 0.0;
            out_values[22] = status.active_commands->discharge_request ? 1.0 : 0.0;
            out_values[23] = status.active_commands->force_shutdown_broadcast ? 1.0 : 0.0;
            out_values[24] = status.active_commands->allow_charging ? 1.0 : 0.0;
            out_values[25] = status.active_commands->fault_shutdown_broadcast ? 1.0 : 0.0;
            out_values[26] = status.active_commands->mos_status ? 1.0 : 0.0;
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_battery_get_status"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_battery_send_passive_commands(std::uint32_t battery_handle, int command_byte0,
                                        int command_byte1) {
    try {
        auto result = encos::wasm::Store().ResolveBattery(battery_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        encos::BatteryPassiveCommands commands{};
        commands.allow_shutdown = (command_byte0 & 0x01) != 0;
        commands.allow_discharge = (command_byte0 & 0x02) != 0;
        commands.parallel_discharge = (command_byte0 & 0x04) != 0;
        commands.force_shutdown = (command_byte0 & 0x08) != 0;
        commands.request_charging = (command_byte0 & 0x10) != 0;
        commands.fault_shutdown_broadcast = (command_byte0 & 0x20) != 0;
        commands.configure_fault_thresholds = (command_byte0 & 0x40) != 0;
        commands.clear_fault = (command_byte0 & 0x80) != 0;
        commands.factory_mode = (command_byte1 & 0x01) != 0;
        commands.debug = (command_byte1 & 0x02) != 0;
        result.value->battery->SendPassiveCommands(commands);
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_battery_send_passive_commands"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_battery_clear_fault(std::uint32_t battery_handle) {
    try {
        auto result = encos::wasm::Store().ResolveBattery(battery_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        result.value->battery->ClearFault();
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_battery_clear_fault"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_battery_request_charging(std::uint32_t battery_handle, int enabled) {
    try {
        auto result = encos::wasm::Store().ResolveBattery(battery_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        result.value->battery->RequestCharging(enabled != 0);
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_battery_request_charging"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_battery_allow_discharge(std::uint32_t battery_handle, int enabled) {
    try {
        auto result = encos::wasm::Store().ResolveBattery(battery_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        result.value->battery->AllowDischarge(enabled != 0);
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_battery_allow_discharge"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_imu_get_status(std::uint32_t imu_handle, double* out_values, int values_len,
                         std::int32_t* out_present, int present_len) {
    try {
        if (RequirePointer(out_values, "out_values") != ErrorCode::Ok ||
            RequirePointer(out_present, "out_present") != ErrorCode::Ok) {
            return ToInt(ErrorCode::InvalidArgument);
        }
        if (values_len < 13 || present_len < 4) {
            return ToInt(encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                                     "IMU status buffers are too small"));
        }
        auto result = encos::wasm::Store().ResolveImu(imu_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        const auto status = result.value->imu->GetStatus();
        for (int i = 0; i < 13; ++i) {
            out_values[i] = 0.0;
        }
        out_present[0] = status.acceleration.has_value() ? 1 : 0;
        out_present[1] = status.angular_velocity.has_value() ? 1 : 0;
        out_present[2] = status.euler_angle.has_value() ? 1 : 0;
        out_present[3] = status.quaternion.has_value() ? 1 : 0;
        if (status.acceleration.has_value()) {
            out_values[0] = status.acceleration->x;
            out_values[1] = status.acceleration->y;
            out_values[2] = status.acceleration->z;
        }
        if (status.angular_velocity.has_value()) {
            out_values[3] = status.angular_velocity->x;
            out_values[4] = status.angular_velocity->y;
            out_values[5] = status.angular_velocity->z;
        }
        if (status.euler_angle.has_value()) {
            out_values[6] = status.euler_angle->pitch;
            out_values[7] = status.euler_angle->roll;
            out_values[8] = status.euler_angle->heading;
        }
        if (status.quaternion.has_value()) {
            out_values[9] = status.quaternion->qw;
            out_values[10] = status.quaternion->qx;
            out_values[11] = status.quaternion->qy;
            out_values[12] = status.quaternion->qz;
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_imu_get_status"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_motor_get_status(std::uint32_t motor_handle, int life_cycle_deduction, double* out_values,
                           int values_len, std::int32_t* out_meta, int meta_len) {
    try {
        if (RequirePointer(out_values, "out_values") != ErrorCode::Ok ||
            RequirePointer(out_meta, "out_meta") != ErrorCode::Ok) {
            return ToInt(ErrorCode::InvalidArgument);
        }
        if (values_len < 5 || meta_len < 4) {
            return ToInt(encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                                     "status buffers are too small"));
        }
        auto result = encos::wasm::Store().ResolveMotor(motor_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        auto status = result.value->motor->GetStatus(life_cycle_deduction);
        out_meta[0] = 1;
        out_meta[1] = ToInt(ErrorCode::Ok);
        out_meta[2] = status.has_value() ? 1 : 0;
        out_meta[3] = status ? static_cast<int>(status->error)
                             : static_cast<int>(encos::MotorError::NoResponse);
        if (status) {
            out_values[0] = status->position;
            out_values[1] = status->speed;
            out_values[2] = status->current;
            out_values[3] = status->motor_temperature;
            out_values[4] = status->mos_temperature;
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_motor_get_status"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_motor_pvt_control(std::uint32_t motor_handle, double kp, double kd, double position,
                            double speed, double torque, int feedback_type, double* out_values,
                            int values_len, std::int32_t* out_meta, int meta_len) {
    try {
        auto code = RequireControlBuffers(out_values, values_len, out_meta, meta_len);
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().ResolveMotor(motor_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        auto& motor = *result.value->motor;
        if (feedback_type == 0) {
            motor.PVTControl<0>(static_cast<float>(kp), static_cast<float>(kd),
                                static_cast<float>(position), static_cast<float>(speed),
                                static_cast<float>(torque));
            FillEmptyControlResult(out_values, out_meta, feedback_type);
        } else if (feedback_type == 1) {
            FillFeedback1(
                motor.PVTControl<1>(static_cast<float>(kp), static_cast<float>(kd),
                                    static_cast<float>(position), static_cast<float>(speed),
                                    static_cast<float>(torque)),
                out_values, out_meta, feedback_type);
        } else {
            return ToInt(encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                                     "pvt feedback_type must be 0 or 1"));
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_motor_pvt_control"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_motor_spd_control(std::uint32_t motor_handle, double speed, double current,
                            int feedback_type, double* out_values, int values_len,
                            std::int32_t* out_meta, int meta_len) {
    try {
        auto code = RequireControlBuffers(out_values, values_len, out_meta, meta_len);
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().ResolveMotor(motor_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        auto& motor = *result.value->motor;
        if (feedback_type == 0) {
            motor.SpdControl<0>(static_cast<float>(speed), static_cast<float>(current), 0);
            FillEmptyControlResult(out_values, out_meta, feedback_type);
        } else if (feedback_type == 1) {
            FillFeedback1(
                motor.SpdControl<1>(static_cast<float>(speed), static_cast<float>(current), 1),
                out_values, out_meta, feedback_type);
        } else if (feedback_type == 2) {
            FillFeedback2(
                motor.SpdControl<2>(static_cast<float>(speed), static_cast<float>(current), 2),
                out_values, out_meta, feedback_type);
        } else if (feedback_type == 3) {
            FillFeedback3(
                motor.SpdControl<3>(static_cast<float>(speed), static_cast<float>(current), 3),
                out_values, out_meta, feedback_type);
        } else {
            return ToInt(encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                                     "feedback_type must be 0, 1, 2 or 3"));
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_motor_spd_control"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_motor_pos_control(std::uint32_t motor_handle, double position, double speed,
                            double current, int feedback_type, double* out_values, int values_len,
                            std::int32_t* out_meta, int meta_len) {
    try {
        auto code = RequireControlBuffers(out_values, values_len, out_meta, meta_len);
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().ResolveMotor(motor_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        auto& motor = *result.value->motor;
        if (feedback_type == 0) {
            motor.PosControl<0>(static_cast<float>(position), static_cast<float>(speed),
                                static_cast<float>(current), 0);
            FillEmptyControlResult(out_values, out_meta, feedback_type);
        } else if (feedback_type == 1) {
            FillFeedback1(
                motor.PosControl<1>(static_cast<float>(position), static_cast<float>(speed),
                                    static_cast<float>(current), 1),
                out_values, out_meta, feedback_type);
        } else if (feedback_type == 2) {
            FillFeedback2(
                motor.PosControl<2>(static_cast<float>(position), static_cast<float>(speed),
                                    static_cast<float>(current), 2),
                out_values, out_meta, feedback_type);
        } else if (feedback_type == 3) {
            FillFeedback3(
                motor.PosControl<3>(static_cast<float>(position), static_cast<float>(speed),
                                    static_cast<float>(current), 3),
                out_values, out_meta, feedback_type);
        } else {
            return ToInt(encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                                     "feedback_type must be 0, 1, 2 or 3"));
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_motor_pos_control"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_motor_cur_control(std::uint32_t motor_handle, double current, int feedback_type,
                            double* out_values, int values_len, std::int32_t* out_meta,
                            int meta_len) {
    try {
        auto code = RequireControlBuffers(out_values, values_len, out_meta, meta_len);
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().ResolveMotor(motor_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        auto& motor = *result.value->motor;
        if (feedback_type == 0) {
            motor.CurControl<0>(static_cast<float>(current), 0);
            FillEmptyControlResult(out_values, out_meta, feedback_type);
        } else if (feedback_type == 1) {
            FillFeedback1(motor.CurControl<1>(static_cast<float>(current), 1), out_values, out_meta,
                          feedback_type);
        } else if (feedback_type == 2) {
            FillFeedback2(motor.CurControl<2>(static_cast<float>(current), 2), out_values, out_meta,
                          feedback_type);
        } else if (feedback_type == 3) {
            FillFeedback3(motor.CurControl<3>(static_cast<float>(current), 3), out_values, out_meta,
                          feedback_type);
        } else {
            return ToInt(encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                                     "feedback_type must be 0, 1, 2 or 3"));
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_motor_cur_control"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_motor_set_pos(std::uint32_t motor_handle, double position, int* out_success) {
    try {
        auto code = RequirePointer(out_success, "out_success");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().ResolveMotor(motor_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_success = result.value->motor->SetPos(position) ? 1 : 0;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_motor_set_pos"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_motor_reset_zero_pos(std::uint32_t motor_handle, int wait_for_ack, int* out_success) {
    try {
        auto code = RequirePointer(out_success, "out_success");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().ResolveMotor(motor_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_success = result.value->motor->ResetZeroPos(wait_for_ack != 0) ? 1 : 0;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_motor_reset_zero_pos"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_motor_get_float_parameter(std::uint32_t motor_handle, int parameter, double* out_value) {
    try {
        auto code = RequirePointer(out_value, "out_value");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().ResolveMotor(motor_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        auto& motor = *result.value->motor;
        switch (static_cast<encos::MotorParameter>(parameter)) {
            case encos::MotorParameter::Position:
                *out_value = motor.GetParameter<encos::MotorParameter::Position>();
                break;
            case encos::MotorParameter::Speed:
                *out_value = motor.GetParameter<encos::MotorParameter::Speed>();
                break;
            case encos::MotorParameter::Current:
                *out_value = motor.GetParameter<encos::MotorParameter::Current>();
                break;
            default:
                return ToInt(encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                                         "unsupported float motor parameter"));
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_motor_get_float_parameter"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_motor_set_can_timeout(std::uint32_t motor_handle, int timeout_ms, int wait_for_ack,
                                int* out_success) {
    try {
        auto code = RequirePointer(out_success, "out_success");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().ResolveMotor(motor_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_success = result.value->motor->SetCanTimeout(static_cast<std::uint16_t>(timeout_ms),
                                                          wait_for_ack != 0)
                           ? 1
                           : 0;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_motor_set_can_timeout"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_seed_motor(std::uint32_t adapter_handle, int bus_idx, int motor_idx, int model) {
    try {
        auto code = encos::wasm::Store().SeedFakeMotor(adapter_handle, bus_idx, motor_idx,
                                                       static_cast<encos::MotorModel>(model));
        if (code != ErrorCode::Ok) {
            encos::wasm::Store().SetLastError(code, "failed to seed fake motor");
            return ToInt(code);
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_seed_motor"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_enable_auto_create_motor(std::uint32_t adapter_handle) {
    try {
        auto code = encos::wasm::Store().EnableFakeAutoCreateMotor(adapter_handle);
        if (code != ErrorCode::Ok) {
            encos::wasm::Store().SetLastError(code, "failed to enable fake auto-create");
            return ToInt(code);
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_enable_auto_create_motor"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_set_reply_mode(std::uint32_t adapter_handle, int reply_mode) {
    try {
        auto code = encos::wasm::Store().SetFakeReplyMode(
            adapter_handle, static_cast<encos::FakeReplyMode>(reply_mode));
        if (code != ErrorCode::Ok) {
            encos::wasm::Store().SetLastError(code, "failed to set fake reply mode");
            return ToInt(code);
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_set_reply_mode"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_inject_feedback(std::uint32_t adapter_handle, int bus_idx, int motor_idx,
                               int feedback_type, const double* status_values, int values_len,
                               const std::int32_t* status_meta, int meta_len) {
    try {
        if (RequirePointer(status_values, "status_values") != ErrorCode::Ok ||
            RequirePointer(status_meta, "status_meta") != ErrorCode::Ok) {
            return ToInt(ErrorCode::InvalidArgument);
        }
        if (values_len < 5 || meta_len < 1) {
            return ToInt(encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                                     "feedback buffers are too small"));
        }
        if (feedback_type < 1 || feedback_type > 3) {
            return ToInt(encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                                     "feedback_type must be 1, 2 or 3"));
        }

        encos::MotorStatus status{};
        status.error = static_cast<encos::MotorError>(status_meta[0]);
        status.position = static_cast<float>(status_values[0]);
        status.speed = static_cast<float>(status_values[1]);
        status.current = static_cast<float>(status_values[2]);
        status.motor_temperature = static_cast<float>(status_values[3]);
        status.mos_temperature = static_cast<float>(status_values[4]);

        auto code = encos::wasm::Store().InjectFakeFeedback(adapter_handle, bus_idx, motor_idx,
                                                            status, feedback_type);
        if (code != ErrorCode::Ok) {
            encos::wasm::Store().SetLastError(code, "failed to inject fake feedback");
            return ToInt(code);
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_inject_feedback"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_set_parameter_write_policy(std::uint32_t adapter_handle, int bus_idx, int motor_idx,
                                          int parameter, int policy) {
    try {
        auto code = encos::wasm::Store().SetFakeParameterWritePolicy(
            adapter_handle, bus_idx, motor_idx, static_cast<encos::MotorParameter>(parameter),
            static_cast<encos::FakeWritePolicy>(policy));
        if (code != ErrorCode::Ok) {
            encos::wasm::Store().SetLastError(code, "failed to set fake parameter policy");
            return ToInt(code);
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_set_parameter_write_policy"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_inject_raw_message(std::uint32_t adapter_handle, int bus_idx, std::uint32_t can_id,
                                  int frame_flags, const std::uint8_t* data, int len) {
    try {
        auto code = encos::wasm::Store().InjectFakeRawMessage(
            adapter_handle, bus_idx, can_id, static_cast<std::uint8_t>(frame_flags), data, len);
        if (code != ErrorCode::Ok) {
            encos::wasm::Store().SetLastError(code, "failed to inject fake raw message");
            return ToInt(code);
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_inject_raw_message"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_get_motor_snapshot(std::uint32_t adapter_handle, int bus_idx, int motor_idx,
                                  double* out_values, int values_len, std::int32_t* out_meta,
                                  int meta_len) {
    try {
        if (RequirePointer(out_values, "out_values") != ErrorCode::Ok ||
            RequirePointer(out_meta, "out_meta") != ErrorCode::Ok) {
            return ToInt(ErrorCode::InvalidArgument);
        }
        if (values_len < 21 || meta_len < 6) {
            return ToInt(encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                                     "snapshot buffers are too small"));
        }
        auto fake_result = encos::wasm::Store().ResolveFakeAdapter(adapter_handle);
        if (!fake_result.Ok()) {
            encos::wasm::Store().SetLastError(fake_result.code, fake_result.message);
            return ToInt(fake_result.code);
        }
        const auto snapshot = fake_result.value->GetMotorSnapshot(bus_idx, motor_idx);
        out_values[0] = snapshot.ranges.kp.min;
        out_values[1] = snapshot.ranges.kp.max;
        out_values[2] = snapshot.ranges.kd.min;
        out_values[3] = snapshot.ranges.kd.max;
        out_values[4] = snapshot.ranges.position.min;
        out_values[5] = snapshot.ranges.position.max;
        out_values[6] = snapshot.ranges.speed.min;
        out_values[7] = snapshot.ranges.speed.max;
        out_values[8] = snapshot.ranges.torque.min;
        out_values[9] = snapshot.ranges.torque.max;
        out_values[10] = snapshot.ranges.current.min;
        out_values[11] = snapshot.ranges.current.max;
        out_values[12] = snapshot.ranges.kt;
        out_values[13] = snapshot.position_rad;
        out_values[14] = snapshot.speed_rad_s;
        out_values[15] = snapshot.current_a;
        out_values[16] = snapshot.torque_nm;
        out_values[17] = snapshot.motor_temp_c;
        out_values[18] = snapshot.mos_temp_c;
        out_values[19] = snapshot.acceleration;
        out_values[20] = snapshot.kt;
        out_meta[0] = static_cast<int>(snapshot.model);
        out_meta[1] = snapshot.can_timeout_ms;
        out_meta[2] = snapshot.reply_frame_flags;
        out_meta[3] = static_cast<int>(snapshot.communication_mode);
        out_meta[4] = static_cast<int>(snapshot.error);
        out_meta[5] = snapshot.brake_enabled ? 1 : 0;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_get_motor_snapshot"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_get_raw_message_count(std::uint32_t adapter_handle, int* out_count) {
    try {
        auto code = RequirePointer(out_count, "out_count");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = GetFakeRawMessageCount(adapter_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_count = result.value;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_get_raw_message_count"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_get_raw_message_bus_index(std::uint32_t adapter_handle, int message_index,
                                         int* out_bus_idx) {
    try {
        auto code = RequirePointer(out_bus_idx, "out_bus_idx");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = GetFakeRawMessage(adapter_handle, message_index);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_bus_idx = result.value.bus_idx;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_get_raw_message_bus_index"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_get_raw_message_can_id(std::uint32_t adapter_handle, int message_index,
                                      int* out_can_id) {
    try {
        auto code = RequirePointer(out_can_id, "out_can_id");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = GetFakeRawMessage(adapter_handle, message_index);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_can_id = static_cast<int>(result.value.data.id);
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_get_raw_message_can_id"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_get_raw_message_frame_flags(std::uint32_t adapter_handle, int message_index,
                                           int* out_frame_flags) {
    try {
        auto code = RequirePointer(out_frame_flags, "out_frame_flags");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = GetFakeRawMessage(adapter_handle, message_index);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_frame_flags = static_cast<int>(result.value.data.frame_flags);
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_get_raw_message_frame_flags"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_get_raw_message_len(std::uint32_t adapter_handle, int message_index, int* out_len) {
    try {
        auto code = RequirePointer(out_len, "out_len");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = GetFakeRawMessage(adapter_handle, message_index);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_len = static_cast<int>(result.value.data.len);
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_get_raw_message_len"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_get_raw_message_data_byte(std::uint32_t adapter_handle, int message_index,
                                         int byte_index, int* out_byte) {
    try {
        auto code = RequirePointer(out_byte, "out_byte");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = GetFakeRawMessage(adapter_handle, message_index);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        if (byte_index < 0 || byte_index >= static_cast<int>(result.value.data.len)) {
            return ToInt(encos::wasm::SetResultError(ErrorCode::InvalidArgument,
                                                     "byte_index is out of range"));
        }
        *out_byte = static_cast<int>(result.value.data.data[byte_index]);
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_get_raw_message_data_byte"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_get_command_count(std::uint32_t adapter_handle, int* out_count) {
    try {
        auto code = RequirePointer(out_count, "out_count");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = encos::wasm::Store().FakeCommandCount(adapter_handle);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_count = result.value;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_get_command_count"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_get_command_kind(std::uint32_t adapter_handle, int command_index, int* out_kind) {
    try {
        auto code = RequirePointer(out_kind, "out_kind");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = GetFakeCommand(adapter_handle, command_index);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_kind = static_cast<int>(result.value.kind);
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_get_command_kind"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_get_command_bus_index(std::uint32_t adapter_handle, int command_index,
                                     int* out_bus_idx) {
    try {
        auto code = RequirePointer(out_bus_idx, "out_bus_idx");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = GetFakeCommand(adapter_handle, command_index);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_bus_idx = result.value.bus_idx;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_get_command_bus_index"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_get_command_motor_index(std::uint32_t adapter_handle, int command_index,
                                       int* out_motor_idx) {
    try {
        auto code = RequirePointer(out_motor_idx, "out_motor_idx");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = GetFakeCommand(adapter_handle, command_index);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        *out_motor_idx = result.value.motor_idx;
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_get_command_motor_index"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_get_command_number_field(std::uint32_t adapter_handle, int command_index,
                                        int field_id, double* out_value) {
    try {
        auto code = RequirePointer(out_value, "out_value");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = GetFakeCommand(adapter_handle, command_index);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        const auto& record = result.value;
        if (const auto* payload = PayloadAs<encos::FakeSpdControlPayload>(record)) {
            if (field_id == 1) {
                *out_value = payload->speed;
            } else if (field_id == 2) {
                *out_value = payload->current;
            } else {
                return ToInt(ErrorCode::InvalidArgument);
            }
        } else if (const auto* payload = PayloadAs<encos::FakePVTControlPayload>(record)) {
            if (field_id == 1) {
                *out_value = payload->kp;
            } else if (field_id == 2) {
                *out_value = payload->kd;
            } else if (field_id == 3) {
                *out_value = payload->position;
            } else if (field_id == 4) {
                *out_value = payload->speed;
            } else if (field_id == 5) {
                *out_value = payload->torque;
            } else {
                return ToInt(ErrorCode::InvalidArgument);
            }
        } else if (const auto* payload = PayloadAs<encos::FakePosControlPayload>(record)) {
            if (field_id == 3) {
                *out_value = payload->position;
            } else if (field_id == 1) {
                *out_value = payload->speed;
            } else if (field_id == 2) {
                *out_value = payload->current;
            } else {
                return ToInt(ErrorCode::InvalidArgument);
            }
        } else if (const auto* payload = PayloadAs<encos::FakeCurControlPayload>(record)) {
            if (field_id == 1) {
                *out_value = payload->current;
            } else {
                return ToInt(ErrorCode::InvalidArgument);
            }
        } else if (const auto* payload = PayloadAs<encos::FakeSetPosPayload>(record)) {
            if (field_id == 1) {
                *out_value = payload->position_rad;
            } else {
                return ToInt(ErrorCode::InvalidArgument);
            }
        } else {
            return ToInt(ErrorCode::InvalidArgument);
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_get_command_number_field"));
    }
}

EMSCRIPTEN_KEEPALIVE
int encos_fake_get_command_int_field(std::uint32_t adapter_handle, int command_index, int field_id,
                                     int* out_value) {
    try {
        auto code = RequirePointer(out_value, "out_value");
        if (code != ErrorCode::Ok) {
            return ToInt(code);
        }
        auto result = GetFakeCommand(adapter_handle, command_index);
        if (!result.Ok()) {
            encos::wasm::Store().SetLastError(result.code, result.message);
            return ToInt(result.code);
        }
        const auto& record = result.value;
        if (const auto* payload = PayloadAs<encos::FakeSpdControlPayload>(record)) {
            if (field_id != 1) {
                return ToInt(ErrorCode::InvalidArgument);
            }
            *out_value = payload->feedback_type;
        } else if (const auto* payload = PayloadAs<encos::FakePosControlPayload>(record)) {
            if (field_id != 1) {
                return ToInt(ErrorCode::InvalidArgument);
            }
            *out_value = payload->feedback_type;
        } else if (const auto* payload = PayloadAs<encos::FakeCurControlPayload>(record)) {
            if (field_id != 1) {
                return ToInt(ErrorCode::InvalidArgument);
            }
            *out_value = payload->feedback_type;
        } else if (const auto* payload = PayloadAs<encos::FakeSetParameterPayload>(record)) {
            if (field_id == 2) {
                *out_value = payload->parameter ? static_cast<int>(*payload->parameter) : -1;
            } else {
                return ToInt(ErrorCode::InvalidArgument);
            }
        } else {
            return ToInt(ErrorCode::InvalidArgument);
        }
        encos::wasm::SetOk();
        return ToInt(ErrorCode::Ok);
    } catch (...) {
        return ToInt(CatchException("encos_fake_get_command_int_field"));
    }
}

}  // extern "C"
