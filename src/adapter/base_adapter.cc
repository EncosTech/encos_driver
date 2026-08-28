#include "adapter/base_adapter.h"

#include <iterator>
#include <stdexcept>
#include <utility>

#include "adapter/base_adapter_impl.h"
#include "adapter/fake_adapter_control.h"
#include "bus/bus_impl.h"
#include "encos/driver_manager.h"
#include "utils/tracy.h"

namespace encos {

BaseAdapter::BaseAdapter(const std::string& interface_name, const std::string& logger_name,
                         LogLevel log_level) {
    impl_ = std::make_unique<Impl>();
    impl_->logger_ = CreateLogger(logger_name, log_level);
    impl_->interface_name = interface_name;
}

BaseAdapter::~BaseAdapter() = default;

FakeAdapterControl::~FakeAdapterControl() = default;

FakeAdapterControl* BaseAdapter::GetFakeAdapterControl() {
    return nullptr;
}

LoggerPtr BaseAdapter::Logger() const {
    return impl_->logger_;
}

std::string BaseAdapter::GetInterfaceName() const {
    return impl_->interface_name;
}

Bus* BaseAdapter::GetBus(int idx) {
    return EncosDriverManager::Instance().CreateBus(this, idx);
}

Bus* BaseAdapter::GetBus(int slave_idx, int bus_idx) {
    int idx = (slave_idx << 16) | (bus_idx & 0xFF);
    return GetBus(idx);
}

Glove* BaseAdapter::GetGlove(int slave_idx) {
    return EncosDriverManager::Instance().CreateGlove(this, slave_idx);
}

void BaseAdapter::OnMessage(const MotorMessages& messages) {
    auto& manager = EncosDriverManager::Instance();
    auto operation = manager.TryAcquireAdapterReceive(this);
    if (!operation) {
        return;
    }
    platform::LockGuard<platform::Mutex> lock(impl_->receive_mutex);
    impl_->OnMessage(*this, messages);
}

void BaseAdapter::RelaySendRaw(const MotorMessage& message) {
    RelaySendRaw(MotorMessages{message});
}

void BaseAdapter::RelaySendRaw(const MotorMessages& messages) {
    if (messages.empty()) {
        return;
    }
    ENCOS_TRACY_ZONE("Adapter::RelaySendRaw");
    platform::LockGuard<decltype(impl_->submit_mutex)> submit_lock(impl_->submit_mutex);
    Send(messages);
}

void BaseAdapter::SetRelayRawMessageCallback(std::function<void(const MotorMessages&)> callback) {
    platform::LockGuard<platform::Mutex> lock(impl_->relay_raw_callback_mutex);
    impl_->relay_raw_callback = std::move(callback);
}

std::map<std::int64_t, MotorStatus> BaseAdapter::GetMotorStatus() {
    return EncosDriverManager::Instance().GetAdapterMotorStatus(this);
}

std::optional<MotorStatus> BaseAdapter::GetMotorStatus(int bus_idx, int motor_idx,
                                                       int life_cycle_deduction) {
    return EncosDriverManager::Instance().GetAdapterMotorStatus(this, bus_idx, motor_idx,
                                                                life_cycle_deduction);
}

void BaseAdapter::SetMaxStatusLifeCycle(int max_life_cycle) {
    EncosDriverManager::Instance().ConfigureAdapterStatusLifeCycle(this, max_life_cycle);
}

int BaseAdapter::GetMaxStatusLifeCycle() const {
    return EncosDriverManager::Instance().GetAdapterStatusLifeCycle(const_cast<BaseAdapter*>(this));
}

void BaseAdapter::SetStatusMedianFilterWindowSize(std::size_t window_size) {
    EncosDriverManager::Instance().ConfigureAdapterStatusMedianFilter(this, window_size);
}

void BaseAdapter::SetStatusLimitFilterMaxDeltas(float position, float speed, float current,
                                                float motor_temperature, float mos_temperature) {
    MotorStatus max_deltas{};
    max_deltas.position = position;
    max_deltas.speed = speed;
    max_deltas.current = current;
    max_deltas.motor_temperature = motor_temperature;
    max_deltas.mos_temperature = mos_temperature;
    EncosDriverManager::Instance().ConfigureAdapterStatusLimitFilter(this, max_deltas);
}

void BaseAdapter::SetOnStatus(int bus_idx, int motor_idx,
                              std::function<void(const MotorStatus&)> callback) {
    EncosDriverManager::Instance().ConfigureAdapterStatusCallback(this, bus_idx, motor_idx,
                                                                  std::move(callback));
}

void BaseAdapter::Send(const MotorMessages& messages) {
    for (const auto& message : messages) {
        Send(message);
    }
}

void BaseAdapter::SendSynchronized(const MotorMessages& messages) {
    Send(messages);
}

void BaseAdapter::Commit() {
    ENCOS_TRACY_ZONE("Adapter::Commit");
    platform::UniqueLock<decltype(impl_->submit_mutex)> submit_lock(impl_->submit_mutex);
    MotorMessages messages;
    impl_->default_sync_mode = true;
    for (const auto& [idx, bus] : GetKnownBusesSnapshot()) {
        (void) idx;
        auto operation = EncosDriverManager::Instance().TryAcquireBusOperation(bus);
        if (!operation) {
            continue;
        }
        platform::LockGuard<decltype(bus->impl_->outgoing_mutex)> lock(bus->impl_->outgoing_mutex);
        const bool was_synchronized = bus->impl_->sync_mode;
        bus->impl_->sync_mode = true;
        if (was_synchronized) {
            auto batch = bus->impl_->DrainOutgoingLocked();
            messages.insert(messages.end(), std::make_move_iterator(batch.begin()),
                            std::make_move_iterator(batch.end()));
        }
    }
    if (!messages.empty()) {
        SendSynchronized(messages);
    }
}

void BaseAdapter::SetSyncMode(bool enabled) {
    ENCOS_TRACY_ZONE("Adapter::SetSyncMode");
    platform::UniqueLock<decltype(impl_->submit_mutex)> submit_lock(impl_->submit_mutex);
    impl_->default_sync_mode = enabled;
    MotorMessages messages;
    for (const auto& [idx, bus] : GetKnownBusesSnapshot()) {
        (void) idx;
        auto operation = EncosDriverManager::Instance().TryAcquireBusOperation(bus);
        if (!operation) {
            continue;
        }
        platform::LockGuard<decltype(bus->impl_->outgoing_mutex)> lock(bus->impl_->outgoing_mutex);
        const bool was_synchronized = bus->impl_->sync_mode;
        bus->impl_->sync_mode = enabled;
        if (!enabled && was_synchronized) {
            auto batch = bus->impl_->DrainOutgoingLocked();
            messages.insert(messages.end(), std::make_move_iterator(batch.begin()),
                            std::make_move_iterator(batch.end()));
        }
    }
    if (!messages.empty()) {
        Send(messages);
    }
}

std::function<void(const MotorPackMsg&)> BaseAdapter::MakeDeviceWriter(Bus* bus) {
    if (bus == nullptr) {
        throw std::invalid_argument("Bus is null");
    }
    auto channel = std::make_shared<Bus::Impl::SendChannel>(bus);
    {
        platform::LockGuard<decltype(bus->impl_->outgoing_mutex)> lock(bus->impl_->outgoing_mutex);
        bus->impl_->send_channels.push_back(channel.get());
    }
    return [channel = std::move(channel)](const MotorPackMsg& message) {
        channel->Push(message);
    };
}

void BaseAdapter::InitializeBusSyncMode(Bus* bus, std::function<void()> publish) {
    platform::LockGuard<decltype(impl_->submit_mutex)> submit_lock(impl_->submit_mutex);
    {
        platform::LockGuard<decltype(bus->impl_->outgoing_mutex)> lock(bus->impl_->outgoing_mutex);
        bus->impl_->sync_mode = impl_->default_sync_mode;
    }
    publish();
}

void BaseAdapter::SubmitDeviceMessage(Bus* bus, void* channel, const MotorMessage& message) {
    ENCOS_TRACY_ZONE("Adapter::SubmitDeviceMessage");
    platform::LockGuard<decltype(impl_->submit_mutex)> submit_lock(impl_->submit_mutex);
    {
        platform::LockGuard<decltype(bus->impl_->outgoing_mutex)> lock(bus->impl_->outgoing_mutex);
        if (bus->impl_->sync_mode) {
            static_cast<Bus::Impl::SendChannel*>(channel)->port.Push(message);
            return;
        }
    }
    try {
        Send(message);
    } catch (const std::exception& exception) {
        if (impl_->logger_) {
            impl_->logger_->error("Direct Bus device submission failed: {}", exception.what());
        }
    } catch (...) {
        if (impl_->logger_) {
            impl_->logger_->error("Direct Bus device submission failed");
        }
    }
}

void BaseAdapter::SubmitBusMessages(Bus* bus, const MotorMessages& messages) {
    if (messages.empty()) {
        return;
    }
    ENCOS_TRACY_ZONE("Adapter::SubmitBusMessages");
    platform::LockGuard<decltype(impl_->submit_mutex)> submit_lock(impl_->submit_mutex);
    {
        platform::LockGuard<decltype(bus->impl_->outgoing_mutex)> lock(bus->impl_->outgoing_mutex);
        if (bus->impl_->sync_mode) {
            bus->impl_->bulk_queue.insert(bus->impl_->bulk_queue.end(), messages.begin(),
                                          messages.end());
            return;
        }
    }
    Send(messages);
}

void BaseAdapter::CommitBus(Bus* bus) {
    ENCOS_TRACY_ZONE("Bus::Commit");
    platform::LockGuard<decltype(impl_->submit_mutex)> submit_lock(impl_->submit_mutex);
    MotorMessages messages;
    {
        platform::LockGuard<decltype(bus->impl_->outgoing_mutex)> lock(bus->impl_->outgoing_mutex);
        bus->impl_->sync_mode = true;
        messages = bus->impl_->DrainOutgoingLocked();
    }
    if (!messages.empty()) {
        SendSynchronized(messages);
    }
}

void BaseAdapter::SetBusSyncMode(Bus* bus, bool enabled) {
    ENCOS_TRACY_ZONE("Bus::SetSyncMode");
    platform::LockGuard<decltype(impl_->submit_mutex)> submit_lock(impl_->submit_mutex);
    MotorMessages messages;
    {
        platform::LockGuard<decltype(bus->impl_->outgoing_mutex)> lock(bus->impl_->outgoing_mutex);
        const bool was_synchronized = bus->impl_->sync_mode;
        bus->impl_->sync_mode = enabled;
        if (!enabled && was_synchronized) {
            messages = bus->impl_->DrainOutgoingLocked();
        }
    }
    if (!messages.empty()) {
        Send(messages);
    }
}

std::unordered_map<int, Bus*> BaseAdapter::GetKnownBusesSnapshot() {
    return EncosDriverManager::Instance().GetBuses(this);
}

void BaseAdapter::Impl::OnMessage(BaseAdapter& owner, const MotorMessages& messages) {
    std::function<void(const MotorMessages&)> relay_callback;
    {
        platform::LockGuard<platform::Mutex> lock(relay_raw_callback_mutex);
        relay_callback = relay_raw_callback;
    }
    if (relay_callback) {
        const int raw_bus_idx = messages.empty() ? -1 : messages.front().bus_idx;
        EncosDriverManager::Instance().DispatchRawReceiveCallback(&owner, raw_bus_idx,
                                                                  relay_callback, messages);
    }

    for (const auto& message : messages) {
        if (EncosDriverManager::Instance().IsBusRegistered(&owner, message.bus_idx)) {
            if (!EncosDriverManager::Instance().DispatchReceive(&owner, message.bus_idx,
                                                                message.data)) {
                (void) EncosDriverManager::Instance().DispatchUnknownReceive(
                    &owner, message.bus_idx, message.data);
            }
        } else {
            const auto dropped = unknown_bus_drop_count.fetch_add(1) + 1;
            if (dropped % 1024 == 1 && logger_) {
                logger_->warn("Dropped message for unknown bus index {} ({} total)",
                              message.bus_idx, dropped);
            }
        }
    }
}

}  // namespace encos
