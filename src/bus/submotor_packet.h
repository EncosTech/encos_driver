// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "device/motor/can_address.h"
#include "device/motor/types.h"

namespace encos {

inline constexpr std::size_t kSubMotorSlotSize = 9;
inline constexpr uint8_t kSixSubMotorPayloadSize = 54;
inline constexpr uint8_t kSevenSubMotorPayloadSize = 63;

/** @brief 在入队前校验子电机槽；普通报文保留原传输层校验 */
inline void ValidateSubMotorMessage(const MotorMessage& message) {
    if (CanSubId(message.data.id) == 0) {
        return;
    }
    const auto& data = message.data;
    if (data.len > 8 || (data.frame_flags & ~kCanFrameFlagMask) != 0 ||
        !CanFrameFlagsHaveValidCanFdBits(data.frame_flags) ||
        CanFrameFlagsUseRtr(data.frame_flags)) {
        throw std::invalid_argument("Sub-motor commands require at most 8 bytes and no RTR");
    }
}

/** @brief 将逻辑子命令封装为含 DLC 的 54/63 字节 FD 包；聚合时逐子电机保序，普通记录构成分组边界 */
inline MotorMessages PackSubMotorMessages(const MotorMessages& messages, bool aggregate) {
    for (const auto& message : messages) {
        ValidateSubMotorMessage(message);
    }
    struct Group {
        std::array<std::size_t, 7> next{};
        std::vector<std::size_t> packets;
    };
    std::map<std::tuple<int, CanId, uint8_t>, Group> groups;
    MotorMessages result;
    result.reserve(messages.size());
    for (const auto& message : messages) {
        const auto sub_id = CanSubId(message.data.id);
        if (sub_id == 0) {
            result.push_back(message);
            groups.clear();
            continue;
        }
        const auto can_id = PhysicalCanId(message.data.id);
        const auto flags = static_cast<uint8_t>(message.data.frame_flags | kCanFrameFlagFdMask |
                                                (can_id > 0x7FFU ? kCanFrameFlagEff : 0));
        if (!aggregate) {
            groups.clear();
        }
        auto& group = groups[{message.bus_idx, can_id, flags}];
        const auto round = group.next[sub_id - 1]++;
        if (round == group.packets.size()) {
            MotorMessage packet{};
            packet.bus_idx = message.bus_idx;
            packet.data.id = can_id;
            packet.data.frame_flags = flags;
            packet.data.len = kSixSubMotorPayloadSize;
            group.packets.push_back(result.size());
            result.push_back(packet);
        }
        auto& packet = result[group.packets[round]];
        const auto offset = (sub_id - 1U) * kSubMotorSlotSize;
        packet.data.len = std::max(
            packet.data.len, sub_id == 7 ? kSevenSubMotorPayloadSize : kSixSubMotorPayloadSize);
        packet.data.data[offset] = message.data.len;
        std::copy_n(message.data.data, message.data.len, packet.data.data + offset + 1);
    }
    return result;
}

}  // namespace encos
