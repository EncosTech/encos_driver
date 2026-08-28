#pragma once

#include <chrono>

#include "battery/battery_impl.h"
#include "imu/imu_impl.h"
#include "platform/sync.h"
#include "pms/pms_impl.h"

namespace encos {

class DeviceStatusTestAccess {
public:
    static void ExpireBatteryStateAndError(Battery* battery) {
        platform::LockGuard<platform::Mutex> lock(battery->impl_->status_mutex);
        const auto expired = std::chrono::steady_clock::now() - Battery::Impl::state_timeout -
                             std::chrono::milliseconds(1);
        battery->impl_->last_state_update = expired;
        battery->impl_->last_error_update = expired;
    }

    static void ExpireImuAcceleration(Imu* imu) {
        platform::LockGuard<platform::Mutex> lock(imu->impl_->status_mutex);
        imu->impl_->last_acceleration_update = std::chrono::steady_clock::now() -
                                               Imu::Impl::state_timeout -
                                               std::chrono::milliseconds(1);
    }

    static void ExpireAllPmsFrames(Pms* pms) {
        platform::LockGuard<platform::Mutex> lock(pms->impl_->status_mutex);
        const auto expired = std::chrono::steady_clock::now() - Pms::Impl::state_timeout -
                             std::chrono::milliseconds(1);
        pms->impl_->last_base_state_update = expired;
        pms->impl_->last_v48_current_1_to_4_update = expired;
        pms->impl_->last_v48_and_v19_currents_update = expired;
    }
};

}  // namespace encos
