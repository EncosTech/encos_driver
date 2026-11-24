// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include "adapter/adapter_statistics.h"
#include "external/json.hpp"

namespace encos::node {

/** @brief 统计绑定序列化；枚举转换为协议字符串，保留有效零值。 */
inline nlohmann::json StatisticsJson(const std::optional<AdapterStatistics>& statistics) {
    if (!statistics)
        return nullptr;

    nlohmann::json buses = nlohmann::json::array();

    for (const auto& bus : statistics->buses) {
        const char* status = "pending";

        switch (bus.status) {
            case StatisticsStatus::Pending:
                break;
            case StatisticsStatus::Valid:
                status = "valid";
                break;
            case StatisticsStatus::Stale:
                status = "stale";
                break;
        }

        nlohmann::json value = {
            {"slaveId", bus.slave_id}, {"busId", bus.bus_id}, {"status", status}};

        if (bus.age_ms)
            value["ageMs"] = *bus.age_ms;

        if (bus.error_counters) {
            const auto& counters = *bus.error_counters;
            value["errorCounters"] = {
                {"tx", counters.tx},
                {"rx", counters.rx},
                {"kind",
                 counters.kind == ErrorCounterKind::Current ? "current" : "last-error-event"}};
        }

        buses.push_back(std::move(value));
    }

    return nlohmann::json{{"buses", std::move(buses)}};
}

}  // namespace encos::node
