// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "gateway_payload.h"

#include <algorithm>
#include <stdexcept>

#include "utils/emr2/relay_frame.h"
namespace encos::ethernet {
namespace {
bool Valid(const MotorMessage& message) {
    const auto& m = message.data;
    return message.bus_idx >= 0 && message.bus_idx < 8 && m.len <= 64 && !(m.frame_flags & 0xf0) &&
           m.id <= ((m.frame_flags & 1) ? 0x1fffffffU : 0x7ffU) &&
           (!(m.frame_flags & 4) || (m.frame_flags & 2)) &&
           !((m.frame_flags & 8) && (m.frame_flags & 2));
}
}  // namespace
std::vector<uint8_t> EncodePayload(const MotorMessages& messages) {
    if (messages.size() > kMaxPayloadMessages ||
        std::any_of(messages.begin(), messages.end(),
                    [](const auto& m) {
                        return m.data.len > 8;
                    }) ||
        !std::all_of(messages.begin(), messages.end(), Valid))
        throw std::invalid_argument(
            "EMR1 requires at most 80 valid CAN records, buses 0..7, data <=8 bytes");
    return EncodeRelayFrames(RelayFrameType::RelayToHelper, messages);
}
std::optional<MotorMessages> DecodePayload(const uint8_t* data, std::size_t size) {
    if (!data || size < 8 || size > 1472)
        return std::nullopt;
    const auto frames = DecodeRelayPackets(std::vector<uint8_t>(data, data + size));
    if (!frames || frames->empty())
        return std::nullopt;
    const bool legacy = frames->front().protocol == RelayProtocol::Emr1;
    if ((!legacy && frames->size() != 1) || (legacy && size > 1448))
        return std::nullopt;
    MotorMessages messages;
    for (const auto& frame : *frames) {
        if (frame.type != RelayFrameType::HelperToRelay ||
            (legacy && frame.protocol != RelayProtocol::Emr1) ||
            (legacy && messages.size() + frame.records.size() > kMaxPayloadMessages))
            return std::nullopt;
        for (const auto& record : frame.records) {
            if (!Valid(record.message))
                return std::nullopt;
            messages.push_back(record.message);
        }
    }
    return messages;
}
}  // namespace encos::ethernet
