#include "glove/glove_calibrator.h"

#include <stdexcept>
#include <utility>

#include "encos/driver_manager.h"
#include "protocol/route_ids.h"

namespace encos {
namespace {

constexpr uint8_t kCalibrationDlc = 8;
constexpr uint16_t kEncoderMask = 0x03FFu;
constexpr uint8_t kCalibrateAll = 0xA0;
constexpr uint8_t kCalibrateMask = 0xA1;

}  // namespace

GloveCalibrator::GloveCalibrator(std::function<void(const MotorPackMsg&)> writer) {
    impl_ = std::make_unique<Impl>();
    impl_->writer = std::move(writer);
}

GloveCalibrator::~GloveCalibrator() = default;

GloveCalibrationStatus GloveCalibrator::CalibrateAll() {
    MotorPackMsg message{};
    message.id = protocol::kGloveCalibrationId;
    message.frame_flags = kCanFrameFlagEff;
    message.len = kCalibrationDlc;
    message.data[0] = kCalibrateAll;
    return SendAndWait(message);
}

GloveCalibrationStatus GloveCalibrator::CalibrateByMask(uint16_t encoder_mask) {
    if ((encoder_mask & static_cast<uint16_t>(~kEncoderMask)) != 0) {
        throw std::invalid_argument("Glove encoder mask may only use bits [0, 9]");
    }
    MotorPackMsg message{};
    message.id = protocol::kGloveCalibrationId;
    message.frame_flags = kCanFrameFlagEff;
    message.len = kCalibrationDlc;
    message.data[0] = kCalibrateMask;
    message.data[1] = static_cast<uint8_t>(encoder_mask & 0xFFu);
    message.data[2] = static_cast<uint8_t>(encoder_mask >> 8u);
    return SendAndWait(message);
}

GloveCalibrationStatus GloveCalibrator::SendAndWait(const MotorPackMsg& message) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    const auto deadline = std::chrono::steady_clock::now() + Impl::kResponseTimeout;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->response_mutex);
        impl_->response = GloveCalibrationStatus::Timeout;
        impl_->response_received = false;
        impl_->waiting_for_response = true;
    }

    try {
        impl_->writer(message);
    } catch (...) {
        platform::LockGuard<platform::Mutex> lock(impl_->response_mutex);
        impl_->waiting_for_response = false;
        impl_->response_received = false;
        return GloveCalibrationStatus::Timeout;
    }

    platform::UniqueLock<platform::Mutex> lock(impl_->response_mutex);
    const bool completed = impl_->response_condition.wait_until(lock, deadline, [this] {
        return impl_->response_received;
    });
    impl_->waiting_for_response = false;
    if (!completed) {
        impl_->response_received = false;
        return GloveCalibrationStatus::Timeout;
    }
    impl_->response_received = false;
    return impl_->response;
}

void GloveCalibrator::OnMessage(const MotorPackMsg& message) {
    if (message.len != kCalibrationDlc) {
        return;
    }
    const auto status = static_cast<GloveCalibrationStatus>(message.data[0]);
    if (status != GloveCalibrationStatus::Success && status != GloveCalibrationStatus::Failed &&
        status != GloveCalibrationStatus::Limited) {
        return;
    }

    {
        platform::LockGuard<platform::Mutex> lock(impl_->response_mutex);
        if (!impl_->waiting_for_response || impl_->response_received) {
            return;
        }
        impl_->response = status;
        impl_->response_received = true;
    }
    impl_->response_condition.notify_one();
}

}  // namespace encos
