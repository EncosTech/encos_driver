// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once
#include <stdexcept>
#include <string>

namespace encos::ethernet {
/** @brief 固定网段192.168.100.0/24，从站ID为末字节减1（0–252）。 */
inline unsigned SlaveId(const std::string& address) {
    constexpr const char* prefix = "192.168.100.";
    if (address.compare(0, 12, prefix) != 0)
        throw std::invalid_argument("Ethernet slaves require subnet 192.168.100.0/24");
    const auto tail = address.substr(12);
    if (tail.empty() || tail.size() > 3 ||
        tail.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Invalid Ethernet slave ID");
    const auto id = std::stoul(tail);
    if (id < 1 || id > 253 || std::to_string(id) != tail)
        throw std::invalid_argument("Ethernet slave IP last octet must be 1..253");
    return static_cast<unsigned>(id - 1);
}
/** @brief 解码GetBus(slave,can)的路由索引；拒绝保留位及非法通道。 */
inline unsigned BusSlave(int index) {
    const auto id = static_cast<unsigned>(index) >> 16;
    if (index < 0 || id > 252 || (index & 0xffff) > 7)
        throw std::invalid_argument("Ethernet requires GetBus(slave_id 0..252, CAN 0..7)");
    return id;
}
}  // namespace encos::ethernet
