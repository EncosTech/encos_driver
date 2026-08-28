#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "adapter/base_adapter.h"
#include "encos/driver_manager.h"
#include "encos_static_plugin_registry.generated.h"
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

}  // namespace

BaseAdapter* EncosDriverManager::CreateAdapter(const std::string& adapter_type,
                                               const std::string& interface_name,
                                               const std::string& logger_name_,
                                               encos::LogLevel log_level) {
    return CreateAdapterWithFactory(interface_name, [=]() {
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
        return definition->create(interface_name, logger_name, log_level);
    });
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

std::vector<std::string> GetAvailableInterface(const std::string& adapter_type) {
    std::vector<std::string> interfaces;
    if (adapter_type == "Ethercat") {
        interfaces = platform::GetWiredInterfaceNames();
    }
    if (adapter_type == "EthercatIGH") {
        interfaces = platform::GetIghMasterIds();
    }
    if (adapter_type == "UsbSerial" || adapter_type == "Slcan") {
        interfaces = platform::GetSerialPortNames();
    }
#ifdef __linux__
    if (adapter_type == "Can") {
        interfaces = platform::GetCanInterfaceNames();
    }
#endif
    std::sort(interfaces.begin(), interfaces.end());
    return interfaces;
}

void ClearLogger() {
    ClearLoggers();
}

}  // namespace encos
