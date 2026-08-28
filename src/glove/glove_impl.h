#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include "glove/glove.h"
#include "platform/sync.h"

namespace encos {

class GloveCalibrator;
class GloveEncoder;

struct Glove::Impl {
    static constexpr std::size_t kFingerCount = 5;
    static constexpr std::size_t kEncodersPerFinger = 10;
    static constexpr std::size_t kEncoderCount = kFingerCount * kEncodersPerFinger;

    GloveStatus GetStatus();
    void SetOnStatus(std::function<void(const GloveStatus&)> callback);
    void OnEncoderUpdate(std::size_t encoder_idx, GloveEncoderStatus status);

    static GloveCalibrationStatus MergeCalibrationStatus(GloveCalibrationStatus current,
                                                         GloveCalibrationStatus next);

    std::array<GloveEncoder*, kEncoderCount> encoders{};
    std::array<GloveCalibrator*, kFingerCount> calibrators{};
    std::array<Bus*, kFingerCount> buses{};

    platform::Mutex status_mutex_;
    GloveStatus status_{};
    std::array<bool, kEncoderCount> updated_in_cycle_{};
    std::function<void(const GloveStatus&)> on_status_;

    std::mutex calibration_request_mutex_;
};

}  // namespace encos
