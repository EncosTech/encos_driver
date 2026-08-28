#pragma once

#include <functional>
#include <utility>

#include "adapter/base_adapter.h"
#include "adapter/base_adapter_impl.h"

namespace encos {

class BaseAdapterTestAccess {
public:
    static void SetRawMessageCallback(BaseAdapter* adapter,
                                      std::function<void(const MotorMessages&)> callback) {
        adapter->SetRelayRawMessageCallback(std::move(callback));
    }

    static bool SubmitMutexIsLocked(BaseAdapter* adapter) {
        if (adapter->impl_->submit_mutex.try_lock()) {
            adapter->impl_->submit_mutex.unlock();
            return false;
        }
        return true;
    }
};

}  // namespace encos
