#include "glove/glove.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "encos/driver_manager.h"
#include "glove/glove_calibrator.h"
#include "glove/glove_encoder.h"
#include "glove/glove_impl.h"

namespace encos {
GloveStatus Glove::Impl::GetStatus() {
    platform::LockGuard<platform::Mutex> lock(status_mutex_);
    return status_;
}

void Glove::Impl::SetOnStatus(std::function<void(const GloveStatus&)> callback) {
    platform::LockGuard<platform::Mutex> lock(status_mutex_);
    on_status_ = std::move(callback);
}

void Glove::Impl::OnEncoderUpdate(std::size_t encoder_idx, GloveEncoderStatus encoder_status) {
    GloveStatus snapshot;
    std::function<void(const GloveStatus&)> callback;
    {
        platform::LockGuard<platform::Mutex> lock(status_mutex_);
        if (encoder_idx >= kEncoderCount) {
            return;
        }
        status_[encoder_idx / kEncodersPerFinger][encoder_idx % kEncodersPerFinger] =
            encoder_status;
        updated_in_cycle_[encoder_idx] = true;
        if (std::all_of(updated_in_cycle_.begin(), updated_in_cycle_.end(), [](bool updated) {
                return updated;
            })) {
            snapshot = status_;
            callback = on_status_;
            updated_in_cycle_.fill(false);
        } else {
            return;
        }
    }
    if (!callback) {
        return;
    }
    try {
        callback(snapshot);
    } catch (...) {}
}

GloveCalibrationStatus Glove::Impl::MergeCalibrationStatus(GloveCalibrationStatus current,
                                                           GloveCalibrationStatus next) {
    if (current == GloveCalibrationStatus::Failed || next == GloveCalibrationStatus::Failed) {
        return GloveCalibrationStatus::Failed;
    }
    if (current == GloveCalibrationStatus::Limited || next == GloveCalibrationStatus::Limited) {
        return GloveCalibrationStatus::Limited;
    }
    return GloveCalibrationStatus::Success;
}

Glove::Glove(std::array<Bus*, 5> buses) {
    impl_ = std::make_unique<Impl>();
    impl_->buses = buses;
}

Glove::~Glove() = default;

GloveStatus Glove::GetStatus() {
    auto operation = EncosDriverManager::Instance().TryAcquireGloveOperation(this);
    if (!operation) {
        return {};
    }
    return impl_->GetStatus();
}

void Glove::SetOnStatus(std::function<void(const GloveStatus&)> callback) {
    auto operation = EncosDriverManager::Instance().TryAcquireGloveOperation(this);
    if (!operation) {
        return;
    }
    impl_->SetOnStatus(std::move(callback));
}

GloveCalibrationStatus Glove::CalibrateAll() {
    auto operation = EncosDriverManager::Instance().TryAcquireGloveOperation(this);
    if (!operation) {
        throw std::runtime_error("Glove is being destroyed or is no longer available");
    }
    std::lock_guard<std::mutex> request_lock(impl_->calibration_request_mutex_);

    GloveCalibrationStatus result = GloveCalibrationStatus::Success;
    for (uint8_t finger_idx = 0; finger_idx < Impl::kFingerCount; ++finger_idx) {
        const auto response = impl_->calibrators[finger_idx]->CalibrateAll();
        if (response == GloveCalibrationStatus::Timeout) {
            return GloveCalibrationStatus::Timeout;
        }
        result = Impl::MergeCalibrationStatus(result, response);
    }
    return result;
}

GloveCalibrationStatus Glove::CalibrateByMask(uint8_t finger_idx, uint16_t encoder_mask) {
    if (finger_idx >= Impl::kFingerCount) {
        throw std::invalid_argument("Glove finger index must be in range [0, 4]");
    }
    if ((encoder_mask & static_cast<uint16_t>(~uint16_t{0x03FF})) != 0) {
        throw std::invalid_argument("Glove encoder mask may only use bits [0, 9]");
    }
    auto operation = EncosDriverManager::Instance().TryAcquireGloveOperation(this);
    if (!operation) {
        throw std::runtime_error("Glove is being destroyed or is no longer available");
    }
    std::lock_guard<std::mutex> request_lock(impl_->calibration_request_mutex_);
    return impl_->calibrators[finger_idx]->CalibrateByMask(encoder_mask);
}

}  // namespace encos
