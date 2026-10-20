// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "adapter/base_adapter.h"
#include "device/device_plugin.h"
#include "device/device_plugin_host.h"
#include "device/device_plugin_validation.h"
#include "encos/driver_manager.h"
#include "encos/encos_driver.h"
#include "encos_static_device_registry.generated.h"
#include "encos_static_plugin_registry.generated.h"
#include "external/json.hpp"
#include "platform/log.h"
#include "platform/os.h"
#include "platform/sync.h"

namespace encos {

namespace {

platform::Mutex plugin_loader_mutex;
std::once_flag static_plugin_path_warning_once;

const StaticPluginDefinition* FindPluginDefinition(const std::string& adapter_type) {
    const auto& definitions = GetStaticPluginDefinitions();
    auto it = std::find_if(definitions.begin(), definitions.end(),
                           [&adapter_type](const StaticPluginDefinition& definition) {
                               return definition.adapter_type == adapter_type;
                           });
    return it == definitions.end() ? nullptr : &(*it);
}

const StaticDevicePluginDefinition* FindDeviceDefinition(const std::string& device_type) {
    const auto& definitions = GetStaticDevicePluginDefinitions();
    auto it = std::find_if(definitions.begin(), definitions.end(),
                           [&device_type](const StaticDevicePluginDefinition& definition) {
                               return definition.device_type == device_type;
                           });
    return it == definitions.end() ? nullptr : &(*it);
}

}  // namespace

const DeviceDriverDescriptorV1* LookupDeviceDriver(const std::string& device_type) {
    const auto* definition = FindDeviceDefinition(device_type);
    if (definition == nullptr || definition->create == nullptr) {
        throw std::runtime_error("PluginLoader: Static device type not found: " + device_type);
    }
    const DeviceDriverDescriptorV1* descriptor = definition->create();
    ValidateDeviceDescriptor(device_type, descriptor);
    return descriptor;
}

namespace {
const bool device_driver_resolver_registered = []() {
    SetDeviceDriverResolver(&LookupDeviceDriver);
    return true;
}();
}  // namespace

BaseAdapter* EncosDriverManager::CreateAdapter(const std::string& adapter_type,
                                               const std::string& interface_name,
                                               const std::string& logger_name,
                                               encos::LogLevel log_level) {
    return CreateAdapter(adapter_type, interface_name, logger_name, log_level,
                         nlohmann::json::object());
}

BaseAdapter* EncosDriverManager::CreateAdapter(const std::string& adapter_type,
                                               const std::string& interface_name,
                                               const std::string& logger_name_,
                                               encos::LogLevel log_level,
                                               const nlohmann::json& extend) {
    if (!extend.is_object()) {
        throw std::runtime_error(
            "PluginLoader: adapter extend configuration MUST be a JSON object");
    }
    const std::string extend_json = extend.dump();
    const std::string update_json = extend.empty() ? std::string{} : extend_json;
    auto* adapter = CreateAdapterWithFactory(interface_name, update_json, [=]() {
        platform::LockGuard<platform::Mutex> lock(plugin_loader_mutex);
        const auto* definition = FindPluginDefinition(adapter_type);
        if (definition == nullptr || definition->create == nullptr) {
            throw std::runtime_error("PluginLoader: Static adapter type not found: " +
                                     adapter_type);
        }
        std::string logger_name = logger_name_;
        if (logger_name.empty()) {
            logger_name = adapter_type + "Adapter";
        }
        if (!definition->supports_extend && !extend.empty()) {
            CreateLogger("PluginLoader", LogLevel::Warn)
                ->warn(
                    "Static adapter plugin '{}' does not declare "
                    "ENCOS_PLUGIN_STATIC_FACTORY_EXTEND; the provided extend configuration "
                    "was ignored",
                    adapter_type);
        }
        return definition->create(interface_name, logger_name, log_level, extend_json);
    });
    return adapter;
}

void SetPluginPath(const std::string& path) {
    (void) path;
    std::call_once(static_plugin_path_warning_once, []() {
        CreateLogger("PluginLoader", LogLevel::Warn)
            ->warn("SetPluginPath() is ignored in static mode because adapters are built in.");
    });
}

std::vector<std::string> GetAvailableAdapterTypes() {
    std::vector<std::string> adapter_types;
    for (const auto& definition : GetStaticPluginDefinitions()) {
        if (definition.include_in_available_types) {
            adapter_types.emplace_back(definition.adapter_type);
        }
    }
    std::sort(adapter_types.begin(), adapter_types.end());
    return adapter_types;
}

std::vector<std::string> GetAvailableDeviceTypes() {
    std::vector<std::string> device_types;
    for (const auto& definition : GetStaticDevicePluginDefinitions()) {
        if (definition.include_in_available_types) {
            device_types.emplace_back(definition.device_type);
        }
    }
    std::sort(device_types.begin(), device_types.end());
    return device_types;
}

namespace {

void AppendInterface(const char* name, void* context) {
    if (name != nullptr) {
        static_cast<std::vector<std::string>*>(context)->emplace_back(name);
    }
}

}  // namespace

std::vector<std::string> GetAvailableInterface(const std::string& adapter_type) {
    const auto* definition = FindPluginDefinition(adapter_type);
    if (definition == nullptr || definition->enumerate == nullptr) {
        throw std::runtime_error("PluginLoader: Static adapter interface enumeration not found: " +
                                 adapter_type);
    }
    std::vector<std::string> interfaces;
    definition->enumerate(&AppendInterface, &interfaces);
    std::sort(interfaces.begin(), interfaces.end());
    return interfaces;
}

void ClearLogger() {
    ClearLoggers();
}

}  // namespace encos
