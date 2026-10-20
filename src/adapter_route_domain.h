// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "platform/sync.h"

namespace encos {

class Bus;
struct RouteRecord;

struct AdapterRouteDomain {
    platform::Mutex mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<RouteRecord>> routes;
    /** @brief 按原始总线索引登记的整总线认领路由；同一总线至多一个 */
    std::unordered_map<int, std::shared_ptr<RouteRecord>> claim_routes;
    std::unordered_map<int, Bus*> buses;
};

}  // namespace encos
