// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "management_protocol.h"

#include <cstring>

#include "ethernet/protocol/gateway_wire.h"
namespace encos::ethernet {
std::optional<CanError> DecodeCanError(const uint8_t* data, std::size_t size,
                                       const std::array<uint8_t, 16>& id) {
    if (!gw_packet_valid(data, size) || data[5] != GW_CAN_EVENT ||
        std::memcmp(data + 12, id.data(), 16) != 0 || gw_get32(data + 32) >= 8)
        return std::nullopt;
    return CanError{gw_get32(data + 8),  gw_get32(data + 28), gw_get32(data + 32),
                    gw_get32(data + 36), gw_get32(data + 40), gw_get32(data + 44),
                    gw_get32(data + 48), gw_get32(data + 52), gw_get32(data + 56)};
}
}  // namespace encos::ethernet
