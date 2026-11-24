// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <stdexcept>

#include "adapter/adapter_extend.h"

namespace encos::ethercat {

inline constexpr auto kDefaultCyclePeriod = std::chrono::microseconds(1000);

/** @brief 提取适配器级周期（1–1000000 微秒），移除该键并保留全部 CAN 配置 */
inline std::chrono::microseconds ExtractCyclePeriod(AdapterExtend& extend) {
    auto remaining = extend.Common();
    auto period = kDefaultCyclePeriod;
    const auto it = remaining.find("cycle_period_us");
    if (it != remaining.end()) {
        if (!it->is_number_integer() || *it < 1 || *it > 1000000) {
            throw std::invalid_argument(
                "EtherCAT cycle_period_us must be an integer in [1, 1000000]");
        }
        period = std::chrono::microseconds(it->get<int>());
        remaining.erase(it);
    }
    for (const auto& bus : extend.DeclaredBuses()) {
        const auto& overrides = *extend.BusOverride(bus.first, bus.second);
        if (overrides.contains("cycle_period_us")) {
            throw std::invalid_argument("EtherCAT cycle_period_us is an adapter-level option");
        }
        remaining["buses"][std::to_string(bus.first) + ":" + std::to_string(bus.second)] =
            overrides;
    }
    extend = AdapterExtend::Parse(remaining.dump());
    return period;
}

/** @brief 在线更新周期必须重建适配器；在任何总线更新前拒绝，避免部分应用 */
inline void RejectCyclePeriodUpdate(const AdapterExtend& extend) {
    if (extend.Common().contains("cycle_period_us")) {
        throw std::invalid_argument(
            "Changing EtherCAT cycle_period_us requires adapter recreation");
    }
    for (const auto& bus : extend.DeclaredBuses()) {
        if (extend.BusOverride(bus.first, bus.second)->contains("cycle_period_us")) {
            throw std::invalid_argument("EtherCAT cycle_period_us is an adapter-level option");
        }
    }
}

}  // namespace encos::ethercat
