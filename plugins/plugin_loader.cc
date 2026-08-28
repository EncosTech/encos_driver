#include <algorithm>
#include <dylib.hpp>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "adapter/base_adapter.h"
#include "encos/driver_manager.h"
#include "encos_hidden_plugin_types.generated.h"
#include "platform/os.h"
#include "platform/sync.h"

namespace encos {

std::map<std::string, std::shared_ptr<dylib::library>> loaded_plugins;
platform::Mutex plugin_loader_mutex;
using CreateAdapterFunc = BaseAdapter* (*) (const char*, const char*, int);

namespace {

bool IsHiddenAdapterType(const std::string& adapter_type) {
    for (const auto& hidden : kHiddenAdapterTypes) {
        if (hidden == adapter_type) {
            return true;
        }
    }
    return false;
}

}  // namespace

BaseAdapter* EncosDriverManager::CreateAdapter(const std::string& adapter_type,
                                               const std::string& interface_name,
                                               const std::string& logger_name_,
                                               encos::LogLevel log_level) {
    return CreateAdapterWithFactory(interface_name, [=]() {
        platform::LockGuard<platform::Mutex> lock(plugin_loader_mutex);
        std::string logger_name = logger_name_;
        if (logger_name.empty()) {
            logger_name = adapter_type + "Adapter";
        }
        auto it = loaded_plugins.find(adapter_type);
        if (it == loaded_plugins.end()) {
            const auto plugin_dir = platform::PluginDir();
            const auto plugin_path = platform::PluginLibraryPath(plugin_dir, adapter_type);
            if (!platform::fs::exists(plugin_path)) {
                throw std::runtime_error("PluginLoader: Plugin file not found: " +
                                         plugin_path.string());
            }
            auto library = std::make_shared<dylib::library>(plugin_path.string());
            it = loaded_plugins.emplace(adapter_type, std::move(library)).first;
        }
        auto create_func =
            it->second->get_function<BaseAdapter*(const char*, const char*, int)>("MakeAdapter");
        if (!create_func) {
            throw std::runtime_error(
                "PluginLoader: Failed to find MakeAdapter function in plugin " + adapter_type);
        }
        return (*create_func)(interface_name.c_str(), logger_name.c_str(),
                              LogLevelToInt(log_level));
    });
}

void SetPluginPath(const std::string& path) {
    platform::SetEnv("ENCOS_PLUGIN_PATH", path);
}

std::vector<std::string> GetAvailableAdapterTypes() {
    std::vector<std::string> adapter_types;
    auto plugin_dir = platform::PluginDir();
    if (platform::fs::exists(plugin_dir) && platform::fs::is_directory(plugin_dir)) {
        for (const auto& entry : platform::fs::directory_iterator(plugin_dir)) {
            if (platform::fs::is_regular_file(entry.path())) {
                std::string filename = entry.path().filename().string();
                std::string adapter_type;
#ifdef _WIN32
                const std::string suffix = "Plugin.dll";
                if (filename.size() > suffix.size() &&
                    filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    adapter_type = filename.substr(0, filename.size() - suffix.size());
                }
#else
                const std::string prefix = "lib";
                const std::string suffix = "Plugin.so";
                if (filename.size() > prefix.size() + suffix.size() &&
                    filename.compare(0, prefix.size(), prefix) == 0 &&
                    filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    adapter_type = filename.substr(prefix.size(),
                                                   filename.size() - prefix.size() - suffix.size());
                }
#endif
                if (adapter_type.empty()) {
                    continue;
                }
                if (IsHiddenAdapterType(adapter_type)) {
                    continue;
                }
                try {
                    auto library = std::make_shared<dylib::library>(entry.path().string());
                    auto create_func = library->get_function<CreateAdapterFunc>("MakeAdapter");
                    if (create_func) {
                        adapter_types.push_back(adapter_type);
                    }
                } catch (const std::exception& e) {
                    // Ignore loading errors
                }
            }
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
