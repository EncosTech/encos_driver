// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "device/device_plugin_host.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bus/bus_impl.h"
#include "device/device.h"
#include "device/device_plugin.h"
#include "driver_manager_impl.h"
#include "encos/driver_manager.h"
#include "external/json.hpp"

namespace encos {

namespace {

std::mutex resolver_mutex;
DeviceDriverResolver device_driver_resolver = nullptr;

/** @brief 规范化配置串的进程级驻留表：节点地址稳定，用作设备身份令牌 */
std::mutex config_intern_mutex;
std::unordered_map<std::string, std::string>& ConfigInternTable() {
    static auto* table = new std::unordered_map<std::string, std::string>();
    return *table;
}

const void* InternConfig(const std::string& canonical) {
    std::lock_guard<std::mutex> lock(config_intern_mutex);
    auto& table = ConfigInternTable();
    auto found = table.find(canonical);
    if (found == table.end()) {
        found = table.emplace(canonical, canonical).first;
    }
    return static_cast<const void*>(&found->second);
}

std::vector<std::uint32_t> ComputeDeviceRouteIds(const DeviceDriverDescriptorV1* descriptor,
                                                 const std::string& config) {
    const std::size_t count = descriptor->fill_route_ids(config.c_str(), nullptr, 0);
    std::vector<std::uint32_t> ids(count);
    if (count != 0) {
        const std::size_t written = descriptor->fill_route_ids(config.c_str(), ids.data(), count);
        if (written != count) {
            throw std::runtime_error("PluginLoader: Device route ID enumeration is not stable");
        }
    }
    return ids;
}

/** @brief 校验能力位与路由 ID 的一致性；未知能力位被忽略以保持前向兼容 */
bool ResolveClaimsBus(const DeviceDriverDescriptorV1* descriptor, const std::string& device_type,
                      const std::vector<std::uint32_t>& can_ids) {
    const bool claims_bus = (descriptor->capabilities & kDeviceCapabilityClaimBus) != 0u;
    const bool has_whole_bus_id =
        std::find(can_ids.begin(), can_ids.end(), kDeviceClaimBusRouteId) != can_ids.end();
    if (claims_bus) {
        if (can_ids.size() != 1u || !has_whole_bus_id) {
            throw std::runtime_error(
                "PluginLoader: Claim-bus device '" + device_type +
                "' MUST return exactly the whole-bus route id from fill_route_ids");
        }
    } else if (has_whole_bus_id) {
        throw std::runtime_error("PluginLoader: Non-claim device '" + device_type +
                                 "' MUST NOT return the whole-bus route id");
    }
    return claims_bus;
}

}  // namespace

void SetDeviceDriverResolver(DeviceDriverResolver resolver) {
    std::lock_guard<std::mutex> lock(resolver_mutex);
    device_driver_resolver = resolver;
}

Device* EncosDriverManager::CreateDevice(Bus* bus, const std::string& device_type,
                                         const nlohmann::json& config) {
    using driver_manager_internal::DeviceKind;
    using driver_manager_internal::DeviceRecipe;
    using driver_manager_internal::DeviceRoute;

    if (bus == nullptr) {
        throw std::invalid_argument("Bus is null");
    }
    DeviceDriverResolver resolver = nullptr;
    {
        std::lock_guard<std::mutex> lock(resolver_mutex);
        resolver = device_driver_resolver;
    }
    if (resolver == nullptr) {
        throw std::runtime_error(
            "PluginLoader: Device driver resolver is not registered in this process");
    }
    const DeviceDriverDescriptorV1* descriptor = resolver(device_type);
    const void* type_token = static_cast<const void*>(descriptor->type_name);
    const std::string canonical_config = config.dump();
    const void* config_token = InternConfig(canonical_config);
    auto can_ids = ComputeDeviceRouteIds(descriptor, canonical_config);
    const bool claims_bus = ResolveClaimsBus(descriptor, device_type, can_ids);
    auto writer = impl_->MakeWriter(bus);
    LoggerPtr logger = bus->impl_->logger_;

    DeviceRecipe recipe;
    recipe.kind = DeviceKind::External;
    recipe.operation_kind = OperationKind::ExternalDevice;
    recipe.can_ids = std::move(can_ids);
    recipe.type_token = type_token;
    recipe.config_token = config_token;
    recipe.claims_bus = claims_bus;
    recipe.construct = [descriptor, bus, device_type, canonical_config, logger, writer]() -> void* {
        DeviceCreateContext context;
        context.bus = bus;
        context.type_name = device_type;
        context.config = canonical_config;
        context.logger = logger;
        context.writer = writer;
        return descriptor->create(context);
    };
    recipe.bind = [](void* device) {
        Device* typed = static_cast<Device*>(device);
        return DeviceRoute{[typed](const MotorPackMsg& message) {
                               typed->OnMessage(message);
                           },
                           [typed]() {
                               typed->CancelWaiters();
                           }};
    };
    recipe.initialize = [](void*) {};
    recipe.commit = [](void*) {};
    recipe.destroy = [descriptor](void* device) {
        descriptor->destroy(static_cast<Device*>(device));
    };
    // 外部设备不携带索引语义，统一以 0 登记；身份由 (bus, type, config) 决定。
    return static_cast<Device*>(impl_->CreateDevice(bus, 0, recipe));
}

}  // namespace encos
