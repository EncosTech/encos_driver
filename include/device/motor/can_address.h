// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace encos {

/** @brief 内部 CAN 逻辑地址：高三位为子 ID，低二十九位为物理 CAN ID */
using CanId = std::uint32_t;
inline constexpr CanId kPhysicalCanIdMask = 0x1FFFFFFFU;

/** @brief 提取内部逻辑地址中的子 ID（0 表示普通设备） */
inline std::uint8_t CanSubId(CanId id) noexcept {
    return static_cast<std::uint8_t>(id >> 29U);
}

/** @brief 提取实际下发到 CAN 总线的 ID */
inline CanId PhysicalCanId(CanId id) noexcept {
    return id & kPhysicalCanIdMask;
}

/** @brief 组合物理 ID 与子 ID，越界时抛出 invalid_argument */
inline CanId MakeCanAddress(CanId can_id, unsigned sub_id) {
    if (can_id > kPhysicalCanIdMask || sub_id > 7U) {
        throw std::invalid_argument("CAN address requires a 29-bit CAN ID and sub ID in [0, 7]");
    }
    return can_id | (static_cast<CanId>(sub_id) << 29U);
}

/** @brief 将完整地址位模式映射到兼容的 int 接口，不截断且不依赖有符号溢出 */
inline int LegacyCanIdBits(CanId id) noexcept {
    static_assert(std::numeric_limits<int>::digits == 31, "CAN compatibility requires 32-bit int");
    return id <= static_cast<CanId>(std::numeric_limits<int>::max())
               ? static_cast<int>(id)
               : -1 - static_cast<int>(std::numeric_limits<CanId>::max() - id);
}

}  // namespace encos
