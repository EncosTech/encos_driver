#include "glove/glove_encoder.h"

#include <optional>
#include <utility>

namespace encos {
namespace {

constexpr float kTwoPi = 6.28318530717958647692F;
constexpr uint8_t kEncoderDlc = 3;

uint16_t ReadU16Le(const uint8_t* data) {
    return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                                 (static_cast<uint16_t>(data[1]) << 8u));
}

}  // namespace

GloveEncoder::GloveEncoder(uint8_t global_idx) {
    impl_ = std::make_unique<Impl>();
    impl_->global_idx = global_idx;
}

GloveEncoder::~GloveEncoder() = default;

void GloveEncoder::OnMessage(const MotorPackMsg& message) {
    if (message.len != kEncoderDlc) {
        return;
    }
    GloveEncoderStatus angle;
    if (message.data[2] != 0) {
        angle = static_cast<float>(ReadU16Le(message.data)) / 65535.0F * kTwoPi;
    }

    impl_->on_status_callback(impl_->global_idx, angle);
}

void GloveEncoder::ConnectStatusCallback(
    std::function<void(std::size_t, GloveEncoderStatus)> callback) {
    impl_->on_status_callback = std::move(callback);
}

}  // namespace encos
