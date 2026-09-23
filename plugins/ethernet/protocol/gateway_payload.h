// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once
#include <optional>
#include <vector>

#include "motor/types.h"
namespace encos::ethernet {
/** @brief 单个 EMR1 UDP 载荷可容纳的最大 CAN 记录数。 */
inline constexpr std::size_t kMaxPayloadMessages = 80;
/** @brief 将不超过 80 条 CAN 记录编码为 EMR1 UDP 载荷。 */
std::vector<uint8_t> EncodePayload(const MotorMessages& messages);
/** @brief 完整校验板端 EMR1 载荷，失败时不返回部分记录。 */
std::optional<MotorMessages> DecodePayload(const uint8_t* data, std::size_t size);
}  // namespace encos::ethernet
