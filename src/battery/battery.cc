#include "battery/battery.h"

#include "battery/battery_impl.h"
#include "encos/driver_manager.h"
#include "protocol/route_ids.h"

namespace encos {

namespace {

uint16_t ReadU16Le(const uint8_t* data) {
    return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                                 (static_cast<uint16_t>(data[1]) << 8));
}

int16_t ReadI16Le(const uint8_t* data) {
    return static_cast<int16_t>(ReadU16Le(data));
}

void SetBit(uint8_t& target, uint8_t bit_index, bool enabled) {
    if (enabled) {
        target = static_cast<uint8_t>(target | (1u << bit_index));
    }
}

}  // namespace

bool Battery::Impl::OnMessage(const MotorPackMsg& message) {
    BatteryStatus snapshot{};
    std::function<void(const BatteryStatus&)> callback;
    bool status_updated = false;
    const auto now = std::chrono::steady_clock::now();
    const auto route_ids = protocol::BatteryStatusIds(idx);
    {
        platform::LockGuard<platform::Mutex> lock(status_mutex);
        if (message.id == route_ids[0] && message.len >= 8) {
            BatteryState parsed{};
            parsed.is_master = message.data[0] != 0;
            parsed.soc = message.data[1] / 100.0f;
            parsed.voltage = ReadU16Le(message.data + 2) * 0.01f;
            parsed.allowed_discharge_current = ReadU16Le(message.data + 4) * 0.00001f;
            parsed.allowed_charge_current = ReadU16Le(message.data + 6) * 0.00001f;
            current_status.state = parsed;
            last_state_update = now;
            status_updated = true;
        } else if (message.id == route_ids[1] && message.len >= 8) {
            BatteryTemp parsed{};
            parsed.battery = static_cast<float>(ReadI16Le(message.data));
            parsed.mos = static_cast<float>(ReadI16Le(message.data + 2));
            parsed.discharge_current = ReadU16Le(message.data + 4) * 0.00001f;
            parsed.charge_current = ReadU16Le(message.data + 6) * 0.00001f;
            current_status.temp = parsed;
            last_temp_update = now;
            status_updated = true;
        } else if (message.id == route_ids[2] && message.len >= 2) {
            BatteryError parsed{};
            parsed.could_not_charge = (message.data[0] & 0x01) != 0;
            parsed.could_not_discharge = (message.data[0] & 0x02) != 0;
            parsed.low_battery = (message.data[0] & 0x04) != 0;
            parsed.over_current_steady = (message.data[0] & 0x08) != 0;
            parsed.over_current_peak = (message.data[0] & 0x10) != 0;
            parsed.over_current_charge = (message.data[0] & 0x20) != 0;
            parsed.battery_over_temp = (message.data[0] & 0x40) != 0;
            parsed.mos_over_temp = (message.data[0] & 0x80) != 0;
            parsed.could_not_communicate = (message.data[1] & 0x01) != 0;
            parsed.stopped_emergency = (message.data[1] & 0x02) != 0;
            parsed.charger_fault = (message.data[1] & 0x04) != 0;
            current_status.error = parsed;
            last_error_update = now;
            status_updated = true;
        } else if (message.id == route_ids[3] && message.len >= 1) {
            BatteryActiveCommands parsed{};
            parsed.shutdown_request = (message.data[0] & 0x01) != 0;
            parsed.discharge_request = (message.data[0] & 0x02) != 0;
            parsed.force_shutdown_broadcast = (message.data[0] & 0x04) != 0;
            parsed.allow_charging = (message.data[0] & 0x08) != 0;
            parsed.fault_shutdown_broadcast = (message.data[0] & 0x10) != 0;
            parsed.mos_status = (message.data[0] & 0x20) != 0;
            current_status.active_commands = parsed;
            last_active_commands_update = now;
            status_updated = true;
        }
        if (status_updated) {
            snapshot = current_status;
            callback = on_status;
        }
    }

    if (status_updated && callback) {
        callback(snapshot);
    }
    return status_updated;
}

BatteryStatus Battery::Impl::GetStatusSnapshot() {
    platform::LockGuard<platform::Mutex> lock(status_mutex);
    auto now = std::chrono::steady_clock::now();
    if (now - last_state_update > state_timeout) {
        current_status.state.reset();
    }
    if (now - last_temp_update > state_timeout) {
        current_status.temp.reset();
    }
    if (now - last_active_commands_update > state_timeout) {
        current_status.active_commands.reset();
    }
    if (now - last_error_update > state_timeout) {
        current_status.error = {.comm_timeout = true};
    }
    return current_status;
}

void Battery::Impl::SendPassiveCommands(const BatteryPassiveCommands& commands) {
    platform::LockGuard<platform::Mutex> lock(command_mutex);
    MotorPackMsg msg{};
    msg.id = 0x4F4u + idx;
    msg.len = 2;
    SetBit(msg.data[0], 0, commands.allow_shutdown);
    SetBit(msg.data[0], 1, commands.allow_discharge);
    SetBit(msg.data[0], 2, commands.parallel_discharge);
    SetBit(msg.data[0], 3, commands.force_shutdown);
    SetBit(msg.data[0], 4, commands.request_charging);
    SetBit(msg.data[0], 5, commands.fault_shutdown_broadcast);
    SetBit(msg.data[0], 6, commands.configure_fault_thresholds);
    SetBit(msg.data[0], 7, commands.clear_fault);
    SetBit(msg.data[1], 0, commands.factory_mode);
    SetBit(msg.data[1], 1, commands.debug);
    writer(msg);
}

Battery::Battery(Bus* bus, uint16_t battery_idx, LoggerPtr logger,
                 std::function<void(const MotorPackMsg&)> writer) {
    impl_ = std::make_unique<Impl>();
    impl_->idx = battery_idx;
    impl_->bus = bus;
    impl_->writer = std::move(writer);
    impl_->logger_ = std::move(logger);
}

Battery::~Battery() = default;

BatteryStatus Battery::GetStatus() {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    return impl_->GetStatusSnapshot();
}

void Battery::OnMessage(const MotorPackMsg& message) {
    (void) impl_->OnMessage(message);
}

void Battery::SetOnStatus(std::function<void(const BatteryStatus&)> callback) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::Mutex> lock(impl_->status_mutex);
    impl_->on_status = std::move(callback);
}

void Battery::SendPassiveCommands(const BatteryPassiveCommands& commands) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    impl_->SendPassiveCommands(commands);
}

void Battery::ClearFault() {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    BatteryPassiveCommands commands{};
    commands.clear_fault = true;
    impl_->SendPassiveCommands(commands);
}

void Battery::RequestCharging(bool enabled) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    BatteryPassiveCommands commands{};
    commands.request_charging = enabled;
    impl_->SendPassiveCommands(commands);
}

void Battery::AllowDischarge(bool enabled) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    BatteryPassiveCommands commands{};
    commands.allow_discharge = enabled;
    impl_->SendPassiveCommands(commands);
}

}  // namespace encos
