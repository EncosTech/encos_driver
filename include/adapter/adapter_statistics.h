// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace encos {

/** @brief 统计采样状态。 */
enum class StatisticsStatus { Pending, Valid, Stale };

/** @brief 错误计数的采样含义。 */
enum class ErrorCounterKind { Current, LastErrorEvent };

/** @brief CAN 控制器发送及接收错误计数；零为有效值。 */
struct CanErrorCounters {
    std::uint32_t tx = 0;
    std::uint32_t rx = 0;

    ErrorCounterKind kind = ErrorCounterKind::Current;
};

/** @brief 总线统计值快照；空错误计数表示没有可用的计数样本。 */
struct BusStatistics {
    int slave_id = 0;
    int bus_id = 0;

    StatisticsStatus status = StatisticsStatus::Pending;
    std::optional<std::uint64_t> age_ms;

    std::optional<CanErrorCounters> error_counters;
};

/** @brief 适配器统计快照；支持但尚未收到数据的总线为 Pending。 */
struct AdapterStatistics {
    std::vector<BusStatistics> buses;
};

}  // namespace encos
