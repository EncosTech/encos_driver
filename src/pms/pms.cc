#include "pms/pms.h"

#include <stdexcept>
#include <utility>

#include "encos/driver_manager.h"
#include "pms/pms_impl.h"

namespace encos {

namespace {

uint16_t ReadU16Le(const uint8_t* data) {
    return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                                 (static_cast<uint16_t>(data[1]) << 8));
}

int16_t ReadI16Le(const uint8_t* data) {
    return static_cast<int16_t>(ReadU16Le(data));
}

}  // namespace

bool Pms::Impl::OnMessage(const MotorPackMsg& message) {
    if (message.len < 8) {
        return false;
    }

    std::optional<PmsStatus> new_base_state;
    std::optional<std::array<float, 4>> new_v48_current_1_to_4;
    std::optional<std::array<float, 4>> new_v48_and_v19_currents;
    if (message.id == protocol::kPmsBaseStateId) {
        PmsStatus parsed{};
        for (size_t channel = 0; channel < parsed.v48_channel_enabled.size(); ++channel) {
            parsed.v48_channel_enabled[channel] = (message.data[0] & (1u << channel)) != 0;
        }
        parsed.battery_soc = message.data[1];
        parsed.battery_voltage = ReadU16Le(message.data + 2) * 0.01f;
        parsed.battery_current = ReadI16Le(message.data + 4) * 0.01f;
        parsed.v5_current = ReadI16Le(message.data + 6) * 0.01f;
        new_base_state = parsed;
    } else if (message.id == protocol::kPmsV48Current1To4Id) {
        std::array<float, 4> parsed{};
        for (size_t channel = 0; channel < parsed.size(); ++channel) {
            parsed[channel] = ReadI16Le(message.data + channel * 2) * 0.01f;
        }
        new_v48_current_1_to_4 = parsed;
    } else if (message.id == protocol::kPmsV48AndV19CurrentId) {
        std::array<float, 4> parsed{};
        for (size_t channel = 0; channel < 2; ++channel) {
            parsed[channel] = ReadI16Le(message.data + channel * 2) * 0.01f;
            parsed[channel + 2] = ReadI16Le(message.data + (channel + 2) * 2) * 0.01f;
        }
        new_v48_and_v19_currents = parsed;
    } else {
        return false;
    }

    PmsStatus snapshot{};
    std::function<void(const PmsStatus&)> callback;
    const auto now = std::chrono::steady_clock::now();
    {
        platform::LockGuard<platform::Mutex> lock(status_mutex);
        if (new_base_state.has_value()) {
            current_status.v48_channel_enabled = new_base_state->v48_channel_enabled;
            current_status.battery_soc = new_base_state->battery_soc;
            current_status.battery_voltage = new_base_state->battery_voltage;
            current_status.battery_current = new_base_state->battery_current;
            current_status.v5_current = new_base_state->v5_current;
            last_base_state_update = now;
            updated_since_callback |= kBaseStateUpdated;
        }
        if (new_v48_current_1_to_4.has_value()) {
            for (size_t channel = 0; channel < new_v48_current_1_to_4->size(); ++channel) {
                current_status.v48_currents[channel] = (*new_v48_current_1_to_4)[channel];
            }
            last_v48_current_1_to_4_update = now;
            updated_since_callback |= kV48Current1To4Updated;
        }
        if (new_v48_and_v19_currents.has_value()) {
            for (size_t channel = 0; channel < 2; ++channel) {
                current_status.v48_currents[channel + 4] = (*new_v48_and_v19_currents)[channel];
                current_status.v19_currents[channel] = (*new_v48_and_v19_currents)[channel + 2];
            }
            last_v48_and_v19_currents_update = now;
            updated_since_callback |= kV48AndV19CurrentUpdated;
        }
        if (updated_since_callback == kAllStateFramesUpdated && IsStatusValid(now)) {
            snapshot = current_status;
            callback = on_status;
            updated_since_callback = 0;
        }
    }

    if (callback) {
        callback(snapshot);
    }
    return callback != nullptr;
}

bool Pms::Impl::IsStatusValid(std::chrono::steady_clock::time_point now) const {
    return now - last_base_state_update <= state_timeout &&
           now - last_v48_current_1_to_4_update <= state_timeout &&
           now - last_v48_and_v19_currents_update <= state_timeout;
}

std::optional<PmsStatus> Pms::Impl::GetStatusSnapshot() {
    platform::LockGuard<platform::Mutex> lock(status_mutex);
    const auto now = std::chrono::steady_clock::now();
    if (!IsStatusValid(now)) {
        return std::nullopt;
    }
    return current_status;
}

void Pms::Impl::SendCommand(PmsCommand command) {
    platform::LockGuard<platform::Mutex> lock(command_mutex);
    const auto raw = static_cast<uint16_t>(command);
    const auto disable_channels = static_cast<uint8_t>(raw & 0x003Fu);
    const auto enable_channels = static_cast<uint8_t>((raw >> 8u) & 0x003Fu);
    if ((disable_channels & enable_channels) != 0) {
        throw std::invalid_argument("PMS command cannot enable and disable the same channel");
    }

    MotorPackMsg message{};
    message.id = protocol::kPmsCommandId;
    message.frame_flags = kCanFrameFlagEff;
    message.len = 8;
    message.data[0] = disable_channels;
    message.data[1] = enable_channels;
    writer(message);
}

Pms::Pms(Bus* bus, LoggerPtr logger, std::function<void(const MotorPackMsg&)> writer) {
    impl_ = std::make_unique<Impl>();
    impl_->bus = bus;
    impl_->writer = std::move(writer);
    impl_->logger_ = std::move(logger);
}

Pms::~Pms() = default;

std::optional<PmsStatus> Pms::GetStatus() {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    return impl_->GetStatusSnapshot();
}

void Pms::OnMessage(const MotorPackMsg& message) {
    (void) impl_->OnMessage(message);
}

void Pms::SetOnStatus(std::function<void(const PmsStatus&)> callback) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::Mutex> lock(impl_->status_mutex);
    impl_->on_status = std::move(callback);
}

void Pms::SendCommand(PmsCommand command) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    impl_->SendCommand(command);
}

}  // namespace encos
