#include "gateway_payload.h"

#include <algorithm>
#include <stdexcept>

#include "utils/emr1/relay_frame.h"
namespace encos::ethernet {
namespace {
bool Valid(const MotorMessage& message) {
    const auto& m = message.data;
    return message.bus_idx >= 0 && message.bus_idx < 8 && m.len <= 8 && !(m.frame_flags & 0xf0) &&
           m.id <= ((m.frame_flags & 1) ? 0x1fffffffU : 0x7ffU) &&
           (!(m.frame_flags & 4) || (m.frame_flags & 2)) &&
           !((m.frame_flags & 8) && (m.frame_flags & 2));
}
}  // namespace
std::vector<uint8_t> EncodePayload(const MotorMessages& messages) {
    if (messages.size() > kMaxPayloadMessages ||
        !std::all_of(messages.begin(), messages.end(), Valid))
        throw std::invalid_argument(
            "EMR1 requires at most 80 valid CAN records, buses 0..7, data <=8 bytes");
    return EncodeRelayFrames(RelayFrameType::RelayToHelper, messages);
}
std::optional<MotorMessages> DecodePayload(const uint8_t* data, std::size_t size) {
    if (!data || size < 8 || size > 1448)
        return std::nullopt;
    const auto frames = DecodeRelayFrames(std::vector<uint8_t>(data, data + size));
    if (!frames)
        return std::nullopt;
    MotorMessages messages;
    for (const auto& frame : *frames) {
        if (frame.type != RelayFrameType::HelperToRelay ||
            messages.size() + frame.records.size() > kMaxPayloadMessages ||
            !std::all_of(frame.records.begin(), frame.records.end(), Valid))
            return std::nullopt;
        messages.insert(messages.end(), frame.records.begin(), frame.records.end());
    }
    return messages;
}
}  // namespace encos::ethernet
