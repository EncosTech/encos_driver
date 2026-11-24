// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>

#include "protocol/gateway_wire.h"

namespace encos::ethernet {

/** @brief 网关错误快照，按端口、DMA、UDP 或 CAN 软件队列分组。 */
struct NetworkDiagnostic {
    uint32_t sequence = 0, uptime = 0, group = 0, index = 0;
    std::array<uint32_t, 6> values{};
};

/**
 * @brief 解码网关诊断事件并校验设备身份、分组索引和保留字段。
 * @param data 管理报文数据。
 * @param size 管理报文字节数。
 * @param identity 订阅时确认的网关身份。
 * @return 校验通过的诊断快照。其他报文类型或校验失败时返回 `std::nullopt`。
 */
inline std::optional<NetworkDiagnostic> DecodeNetworkDiagnostic(
    const uint8_t* data, size_t size, const std::array<uint8_t, 16>& identity) {
    if (!gw_packet_valid(data, size) || data[5] != GW_NETWORK_EVENT ||
        std::memcmp(data + 12, identity.data(), identity.size()) != 0)
        return std::nullopt;

    NetworkDiagnostic e;
    e.sequence = gw_get32(data + 8);
    e.uptime = gw_get32(data + 28);
    e.group = gw_get32(data + 32);
    e.index = gw_get32(data + 36);
    if (e.group > 3 || (e.group == 0 && e.index >= 3) ||
        ((e.group == 1 || e.group == 2) && e.index != 0) || (e.group == 3 && e.index >= 8))
        return std::nullopt;

    for (size_t i = 0; i < e.values.size(); ++i)
        e.values[i] = gw_get32(data + 40 + 4 * i);
    if ((e.group == 2 && (e.values[4] || e.values[5])) ||
        (e.group == 3 && (e.values[3] || e.values[4] || e.values[5])))
        return std::nullopt;
    return e;
}

/** @brief 按分组过滤重复和乱序快照，按 32 位无符号整数回绕计算计数增量。 */
class NetworkDiagnosticTracker {
public:
    /** @brief 清空各组基线，由管理线程在心跳超时后调用。 */
    void Reset() {
        previous_ = {};
    }

    /**
     * @brief 更新对应分组的基线，生成异常计数或状态的日志摘要。
     * @param e 通过 DecodeNetworkDiagnostic 校验的诊断快照。
     * @return 有异常需要报告时返回日志摘要，否则返回 `std::nullopt`。
     */
    std::optional<std::string> Update(const NetworkDiagnostic& e) {
        const size_t slot = e.group == 0 ? e.index : (e.group == 3 ? 5 + e.index : 2 + e.group);
        auto& old = previous_.at(slot);
        if (old && (e.sequence - old->sequence == 0 || e.sequence - old->sequence >= 0x80000000U))
            return std::nullopt;

        const auto before = old;
        old = e;
        if (before && before->values == e.values)
            return std::nullopt;

        const size_t counters = e.group == 0 ? 5 : (e.group == 1 ? 2 : 3);
        bool error = false;
        for (size_t i = 0; i < counters; ++i)
            error |= before ? e.values[i] != before->values[i] : e.values[i] != 0;
        if (e.group == 0)
            error |= e.values[5] != 0 && (!before || e.values[5] != before->values[5]);
        if (e.group == 1)
            error |= (e.values[2] || e.values[3]) && (!before || e.values[2] != before->values[2] ||
                                                      e.values[3] != before->values[3]);
        if (!error)
            return std::nullopt;

        static constexpr const char* names[4][6] = {
            {"rx_fifo_overflow", "rx_error", "rx_drop", "tx_fifo_overflow", "tx_error",
             "fifo_flags"},
            {"timeouts", "tx_error", "quarantined", "cpu_fifo_flags", "dma_status",
             "late_completions"},
            {"bad_packets", "queue_or_session_rejected", "send_error", "last_send_error", "", ""},
            {"rx_queue_drop", "tx_queue_drop", "tx_timeout", "", "", ""}};
        static constexpr const char* scopes[] = {"TSW port", "DMA", "UDP", "CAN queue"};

        std::ostringstream out;
        out << scopes[e.group] << '=' << e.index << (before ? " update" : " baseline(since boot)");
        for (size_t i = 0; i < e.values.size() && names[e.group][i][0]; ++i) {
            out << ' ' << names[e.group][i] << '=';
            if (e.group == 2 && i == 3)
                out << static_cast<int32_t>(e.values[i]);
            else
                out << e.values[i];
            if (before && i < counters)
                out << "(+" << static_cast<uint32_t>(e.values[i] - before->values[i]) << ')';
        }
        return out.str();
    }

private:
    std::array<std::optional<NetworkDiagnostic>, 13> previous_{};
};

}  // namespace encos::ethernet
