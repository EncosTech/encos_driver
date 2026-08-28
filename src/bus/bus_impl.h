#pragma once

#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "bus/bus.h"
#include "platform/sync.h"
#include "utils/port.h"
#include "utils/tracy.h"

namespace encos {

class BaseAdapter;

struct Bus::Impl {
    struct SendChannel {
        explicit SendChannel(Bus* bus) : bus(bus) {}
        ~SendChannel();

        void Push(const MotorPackMsg& message);

        Bus* bus;
        Port<10, MotorMessage> port;
    };

    void Send(const MotorPackMsg& message);
    void Send(const MotorMessages& messages);
    void SetExternalDeviceFlag(bool value);
    MotorMessages DrainOutgoingLocked();

    int idx = -1;
    BaseAdapter* adapter = nullptr;
    LoggerPtr logger_;
    std::function<void(const MotorMessages&)> writer;
    mutable platform::Mutex state_mutex;
    platform::Mutex consumer_mutex;
    Port<512, MotorPackMsg> unknown_messages{BasePort::kAnyCanId};
    bool external_device_flag = false;
    std::vector<SendChannel*> send_channels;
    std::deque<MotorMessage> bulk_queue;
    ENCOS_TRACY_LOCKABLE(platform::Mutex, outgoing_mutex, "Bus::outgoing_mutex");
    bool sync_mode = false;
};

}  // namespace encos
