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
    std::unordered_map<int, Bus*> buses;
};

}  // namespace encos
