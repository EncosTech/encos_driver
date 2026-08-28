#pragma once

#include <atomic>
#include <functional>
#include <string>

#include "adapter/base_adapter.h"
#include "adapter_route_domain.h"
#include "platform/sync.h"
#include "utils/tracy.h"

namespace encos {

struct BaseAdapter::Impl {
    void OnMessage(BaseAdapter& owner, const MotorMessages& messages);

    AdapterRouteDomain route_domain;
    std::atomic<std::size_t> unknown_bus_drop_count{0};
    platform::Mutex receive_mutex;
    std::function<void(const MotorMessages&)> relay_raw_callback;
    platform::Mutex relay_raw_callback_mutex;
    std::string interface_name;
    LoggerPtr logger_;
    ENCOS_TRACY_LOCKABLE(platform::RecursiveMutex, submit_mutex, "Adapter::submit_mutex");
    bool default_sync_mode = false;
};

}  // namespace encos
