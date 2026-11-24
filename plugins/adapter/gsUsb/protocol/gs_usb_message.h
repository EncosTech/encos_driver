// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstring>
#include <optional>

#include "device/motor/types.h"
#include "gs_usb_protocol.h"

namespace encos::gs_usb {

/** @brief 将内部消息转换为 USB 帧；非标准 FD 长度向上补零，非法长度、标志或 ID 返回空 */
inline std::optional<Frame> EncodeMessage(const MotorPackMsg& message, std::uint8_t channel,
                                          std::uint32_t echo_id) {
    const auto flags = SanitizeCanFrameFlags(message.frame_flags);
    const bool fd = CanFrameFlagsUseCanFd(flags);
    const bool extended = CanFrameFlagsUseExtendedId(flags);
    if (!CanFrameFlagsHaveValidCanFdBits(flags) || message.len > (fd ? 64U : 8U) ||
        message.id > (extended ? kCanExtIdMask : kCanStdIdMask)) {
        return std::nullopt;
    }
    Frame frame;
    frame.echo_id = echo_id;
    frame.channel = channel;
    frame.can_id =
        message.id | (extended ? kCanEff : 0U) | (CanFrameFlagsUseRtr(flags) ? kCanRtr : 0U);
    frame.flags = fd ? kFrameFlagFd | kFrameFlagBrs : 0U;
    frame.dlc = fd ? *LengthToDlc(message.len) : message.len;
    if (!PayloadLength(frame).has_value()) {
        return std::nullopt;
    }
    if (!CanFrameFlagsUseRtr(flags)) {
        std::memcpy(frame.data, message.data, message.len);
    }
    return frame;
}

/** @brief 将 USB 帧转换为内部消息；len 返回 DLC 对应的线上字节数，非法帧返回空 */
inline std::optional<MotorPackMsg> DecodeMessage(const Frame& frame) {
    const auto length = PayloadLength(frame);
    if (!length.has_value() || (frame.can_id & kCanErr) != 0U) {
        return std::nullopt;
    }
    MotorPackMsg message{};
    const bool extended = (frame.can_id & kCanEff) != 0U;
    const bool rtr = (frame.can_id & kCanRtr) != 0U;
    message.id = frame.can_id & (extended ? kCanExtIdMask : kCanStdIdMask);
    message.frame_flags = (extended ? kCanFrameFlagEff : 0U) | (rtr ? kCanFrameFlagRtr : 0U) |
                          ((frame.flags & kFrameFlagFd) != 0U ? kCanFrameFlagFdMask : 0U);
    message.len = *length;
    if (!rtr) {
        std::memcpy(message.data, frame.data, message.len);
    }
    return message;
}

}  // namespace encos::gs_usb
