#include "bus/bus.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

#include "adapter/base_adapter.h"
#include "battery/battery.h"
#include "bus/bus_impl.h"
#include "encos/driver_manager.h"
#include "glove/glove.h"
#include "imu/imu.h"
#include "motor/motor.h"
#include "motor/motor_impl.h"
#include "motor/pack_helper.h"
#include "platform/delay.h"
#include "pms/pms.h"

namespace encos {

Bus::Bus(BaseAdapter* adapter, int idx, LoggerPtr logger) : impl_(std::make_unique<Impl>()) {
    impl_->idx = idx;
    impl_->adapter = adapter;
    impl_->logger_ = std::move(logger);
    impl_->writer = [this, adapter](const MotorMessages& messages) {
        adapter->SubmitBusMessages(this, messages);
    };
}

Bus::~Bus() = default;

auto Bus::AcquireOperation() {
    return EncosDriverManager::Instance().AcquireBusOperation(this);
}

auto Bus::TryAcquireOperation() {
    return EncosDriverManager::Instance().TryAcquireBusOperation(this);
}

Motor* Bus::GetMotor(int motor_idx, MotorModel model) {
    auto operation = AcquireOperation();
    return EncosDriverManager::Instance().CreateMotor(this, motor_idx, model);
}

Motor* Bus::GetMotor(int motor_idx, MotorModel model, bool canfd) {
    auto operation = AcquireOperation();
    auto* motor =
        EncosDriverManager::Instance().CreateMotor(this, motor_idx, model, uint8_t{0}, canfd);
    if (canfd) {
        motor->EnableCanFd();
    } else {
        motor->DisableCanFd();
    }
    return motor;
}

Motor* Bus::GetMotor(int motor_idx, MotorPVTRanges ranges) {
    auto operation = AcquireOperation();
    return EncosDriverManager::Instance().CreateMotor(this, motor_idx, ranges);
}

Motor* Bus::GetMotor(int motor_idx, MotorPVTRanges ranges, bool canfd) {
    auto operation = AcquireOperation();
    auto* motor =
        EncosDriverManager::Instance().CreateMotor(this, motor_idx, ranges, uint8_t{0}, canfd);
    if (canfd) {
        motor->EnableCanFd();
    } else {
        motor->DisableCanFd();
    }
    return motor;
}

Motor* Bus::GetMotor(int motor_idx) {
    auto operation = AcquireOperation();
    return EncosDriverManager::Instance().CreateMotor(this, motor_idx);
}

Motor* Bus::GetMotor(int motor_idx, bool canfd) {
    auto operation = AcquireOperation();
    auto* motor = EncosDriverManager::Instance().CreateMotor(this, motor_idx, uint8_t{0}, canfd);
    if (canfd) {
        motor->EnableCanFd();
    } else {
        motor->DisableCanFd();
    }
    return motor;
}

Battery* Bus::GetBattery(int battery_idx) {
    auto operation = AcquireOperation();
    const bool was_external = HasExternalDevice();
    impl_->SetExternalDeviceFlag(true);
    try {
        return EncosDriverManager::Instance().CreateBattery(this, battery_idx);
    } catch (...) {
        if (!was_external && !EncosDriverManager::Instance().HasRegisteredExternalDevice(this)) {
            impl_->SetExternalDeviceFlag(false);
        }
        throw;
    }
}

Imu* Bus::GetImu(int imu_idx) {
    auto operation = AcquireOperation();
    const bool was_external = HasExternalDevice();
    impl_->SetExternalDeviceFlag(true);
    try {
        return EncosDriverManager::Instance().CreateImu(this, imu_idx);
    } catch (...) {
        if (!was_external && !EncosDriverManager::Instance().HasRegisteredExternalDevice(this)) {
            impl_->SetExternalDeviceFlag(false);
        }
        throw;
    }
}

Pms* Bus::GetPms() {
    auto operation = AcquireOperation();
    const bool was_external = HasExternalDevice();
    impl_->SetExternalDeviceFlag(true);
    try {
        return EncosDriverManager::Instance().CreatePms(this);
    } catch (...) {
        if (!was_external && !EncosDriverManager::Instance().HasRegisteredExternalDevice(this)) {
            impl_->SetExternalDeviceFlag(false);
        }
        throw;
    }
}

Motor* Bus::SelectMotor(int motor_idx) {
    auto operation = AcquireOperation();
    return EncosDriverManager::Instance().FindMotor(this, motor_idx);
}

std::unordered_map<int, Motor*> Bus::GetMotors() {
    auto operation = AcquireOperation();
    return EncosDriverManager::Instance().GetMotors(this);
}

bool Bus::DetectExternalDevice() {
    auto operation = TryAcquireOperation();
    if (!operation) {
        return false;
    }
    platform::LockGuard<platform::Mutex> consumer_lock(impl_->consumer_mutex);
    return DetectExternalDeviceLocked();
}

bool Bus::DetectExternalDeviceLocked() {
    return DetectExternalDeviceLockedImpl([](std::chrono::milliseconds delay) {
        platform::SleepFor(delay);
    });
}

bool Bus::DetectExternalDeviceLockedImpl(
    const std::function<void(std::chrono::milliseconds)>& sleep_function) {
    if (EncosDriverManager::Instance().HasRegisteredExternalDevice(this)) {
        impl_->SetExternalDeviceFlag(true);
        return true;
    }
    ClearUnknownMessagesLocked();
    sleep_function(std::chrono::seconds(1));
    const bool detected = !DrainUnknownMessagesLocked().empty();
    impl_->SetExternalDeviceFlag(detected);
    return detected;
}

bool Bus::HasExternalDevice() const {
    auto operation = const_cast<Bus*>(this)->AcquireOperation();
    platform::LockGuard<platform::Mutex> lock(impl_->state_mutex);
    return impl_->external_device_flag;
}

std::unordered_map<int, Motor*> Bus::ScanMotors() {
    return ScanMotorsImpl();
}

std::unordered_map<int, Motor*> Bus::ScanMotorsImpl() {
    auto& manager = EncosDriverManager::Instance();
    auto operation = TryAcquireOperation();
    if (!operation) {
        return {};
    }
    platform::LockGuard<platform::Mutex> consumer_lock(impl_->consumer_mutex);
    if (HasExternalDevice() || DetectExternalDeviceLocked()) {
        if (impl_->logger_) {
            impl_->logger_->warn("Bus {}: External device detected, skipping motor scan",
                                 impl_->idx);
        }
        return {};
    }

    ClearUnknownMessagesLocked();
    auto send_discovery_queries = [this](const auto& candidate_ids, bool canfd) {
        MotorMessages send_chunk;
        send_chunk.reserve(3);
        for (const auto candidate_id : candidate_ids) {
            MotorPackMsg message{};
            message.id = candidate_id;
            message.frame_flags = canfd ? kCanFrameFlagFdMask : 0;
            message.len = 2;
            message.data[0] = static_cast<std::uint8_t>(0x07 << 5);
            message.data[1] = static_cast<std::uint8_t>(MotorParameter::Position);
            send_chunk.push_back(MotorMessage{impl_->idx, message});
            if (send_chunk.size() == 3U) {
                impl_->Send(send_chunk);
                send_chunk.clear();
                platform::SleepFor(std::chrono::milliseconds(1));
            }
        }
        if (!send_chunk.empty()) {
            impl_->Send(send_chunk);
        }
    };

    std::unordered_map<int, uint8_t> discovered_frame_flags;
    auto collect_discovery_replies = [this, &discovered_frame_flags]() {
        for (const auto& message : DrainUnknownMessagesLocked()) {
            const auto& pack = message.data;
            if (pack.id > 0x7FE || pack.len < 6 ||
                pack.data[0] != static_cast<std::uint8_t>(0x05 << 5) ||
                pack.data[1] != static_cast<std::uint8_t>(MotorParameter::Position)) {
                continue;
            }
            discovered_frame_flags[static_cast<int>(pack.id)] |=
                SanitizeCanFrameFlags(pack.frame_flags);
        }
    };

    std::vector<std::uint16_t> all_candidate_ids;
    all_candidate_ids.reserve(0x7FF);
    for (std::uint16_t candidate_id = 0; candidate_id < 0x7FF; ++candidate_id) {
        all_candidate_ids.push_back(candidate_id);
    }
    send_discovery_queries(all_candidate_ids, false);
    platform::SleepFor(std::chrono::milliseconds(500));
    collect_discovery_replies();

    std::vector<std::uint16_t> canfd_candidate_ids;
    canfd_candidate_ids.reserve(discovered_frame_flags.size());
    for (const auto& [motor_idx, frame_flags] : discovered_frame_flags) {
        (void) frame_flags;
        canfd_candidate_ids.push_back(static_cast<std::uint16_t>(motor_idx));
    }
    if (!canfd_candidate_ids.empty()) {
        send_discovery_queries(canfd_candidate_ids, true);
        platform::SleepFor(std::chrono::milliseconds(500));
        collect_discovery_replies();
    }
    for (const auto& [motor_idx, frame_flags] : discovered_frame_flags) {
        if (manager.ReconcileDiscoveredMotor(this, motor_idx, frame_flags,
                                             CanFrameFlagsUseCanFd(frame_flags)) == nullptr) {
            return manager.GetMotors(this);
        }
    }
    return EncosDriverManager::Instance().GetMotors(this);
}

std::unordered_map<int, Motor*> Bus::ScanMotorsForTesting() {
    return ScanMotorsImpl();
}

bool Bus::DetectExternalDeviceLockedForTesting(
    const std::function<void(std::chrono::milliseconds)>& sleep_function) {
    return DetectExternalDeviceLockedImpl(sleep_function);
}

bool Bus::ResetMotorsId(bool wait_for_ack) {
    auto& manager = EncosDriverManager::Instance();
    auto operation = TryAcquireOperation();
    if (!operation) {
        return false;
    }
    platform::LockGuard<platform::Mutex> consumer_lock(impl_->consumer_mutex);
    MotorPackMsg message{};
    message.id = 0x7FF;
    message.len = 6;
    message.data[0] = 0x7F;
    message.data[1] = 0x7F;
    message.data[2] = 0x00;
    message.data[3] = 0x05;
    message.data[4] = 0x7F;
    message.data[5] = 0x7F;
    impl_->Send(message);
    if (!wait_for_ack) {
        return true;
    }
    const auto result = WaitForPacket(
        [this]() {
            return DrainUnknownMessagesLocked();
        },
        [](const MotorPackMsg& pack) {
            return pack.id == 0x7FF && pack.data[0] == 0x7F && pack.data[1] == 0x7F;
        });
    if (!result.has_value() || !*result) {
        return false;
    }
    return manager.ResetBusMotorsToIdOne(this);
}

void Bus::Commit() {
    auto operation = AcquireOperation();
    (void) operation;
    impl_->adapter->CommitBus(this);
}

void Bus::SetSyncMode(bool enabled) {
    auto operation = AcquireOperation();
    (void) operation;
    impl_->adapter->SetBusSyncMode(this, enabled);
}

int Bus::GetBusIndex() const {
    auto operation = const_cast<Bus*>(this)->AcquireOperation();
    return impl_->idx;
}

void Bus::SubmitDeviceMessage(void* channel, const MotorPackMsg& message) {
    MotorMessage outgoing{};
    outgoing.bus_idx = impl_->idx;
    outgoing.data = message;
    outgoing.data.frame_flags = SanitizeCanFrameFlags(outgoing.data.frame_flags);
    impl_->adapter->SubmitDeviceMessage(this, channel, outgoing);
}

void Bus::UnregisterSendChannel(void* channel) {
    // 总线已销毁时写入通道注册表随总线一并失效（如校准设备持写入器晚于
    // 总线销毁的路径），此时无需再反注册。
    if (!EncosDriverManager::Instance().IsBusAlive(this)) {
        return;
    }
    platform::LockGuard<decltype(impl_->outgoing_mutex)> lock(impl_->outgoing_mutex);
    const auto* typed_channel = static_cast<Impl::SendChannel*>(channel);
    const auto found =
        std::find(impl_->send_channels.begin(), impl_->send_channels.end(), typed_channel);
    if (found != impl_->send_channels.end()) {
        impl_->send_channels.erase(found);
    }
}

MotorMessages Bus::DrainUnknownMessagesLocked() {
    MotorMessages result;
    while (auto message = impl_->unknown_messages.Pop()) {
        result.push_back(MotorMessage{impl_->idx, *message});
    }
    return result;
}

void Bus::ClearUnknownMessagesLocked() {
    impl_->unknown_messages.Clear();
}

void Bus::Send(int idx, const MotorPackMsg& message) {
    (void) idx;
    impl_->Send(message);
}

void Bus::Impl::Send(const MotorPackMsg& message) {
    Send(MotorMessages{MotorMessage{idx, message}});
}

void Bus::Impl::Send(const MotorMessages& messages) {
    writer(messages);
}

void Bus::Impl::SetExternalDeviceFlag(bool value) {
    platform::LockGuard<platform::Mutex> lock(state_mutex);
    external_device_flag = value;
}

Bus::Impl::SendChannel::~SendChannel() {
    bus->UnregisterSendChannel(this);
}

void Bus::Impl::SendChannel::Push(const MotorPackMsg& message) {
    bus->SubmitDeviceMessage(this, message);
}

MotorMessages Bus::Impl::DrainOutgoingLocked() {
    MotorMessages messages;
    messages.reserve(send_channels.size() * 10U + bulk_queue.size());
    for (auto* channel : send_channels) {
        for (std::size_t count = 0; count < 10U; ++count) {
            auto message = channel->port.Pop();
            if (!message.has_value()) {
                break;
            }
            messages.push_back(*message);
        }
    }
    messages.insert(messages.end(), std::make_move_iterator(bulk_queue.begin()),
                    std::make_move_iterator(bulk_queue.end()));
    bulk_queue.clear();
    return messages;
}

}  // namespace encos
