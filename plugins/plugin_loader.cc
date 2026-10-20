// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cstdint>
#include <dylib.hpp>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter/adapter_plugin.h"
#include "adapter/base_adapter.h"
#include "device/device_plugin.h"
#include "device/device_plugin_host.h"
#include "device/device_plugin_validation.h"
#include "encos/driver_manager.h"
#include "encos/encos_driver.h"
#include "encos_hidden_device_types.generated.h"
#include "encos_hidden_plugin_types.generated.h"
#include "external/json.hpp"
#include "platform/log.h"
#include "platform/os.h"
#include "platform/sync.h"

namespace encos {

std::map<std::string, std::shared_ptr<dylib::library>> loaded_plugins;
platform::Mutex plugin_loader_mutex;
// 注意：dylib::get_function<T> 返回 T*，因此这里的 T 必须是函数类型本身而非函数指针类型
using CreateAdapterFunc = BaseAdapter*(const char*, const char*, int);
using CreateAdapterExtendFunc = BaseAdapter*(const char*, const char*, int, const char*);

std::map<std::string, std::shared_ptr<dylib::library>> loaded_device_plugins;
platform::Mutex device_plugin_loader_mutex;

namespace {

bool IsHiddenAdapterType(const std::string& adapter_type) {
    for (const auto& hidden : kHiddenAdapterTypes) {
        if (hidden == adapter_type) {
            return true;
        }
    }
    return false;
}

bool IsHiddenDeviceType(const std::string& device_type) {
    for (const auto& hidden : kHiddenDeviceTypes) {
        if (hidden == device_type) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<dylib::library> AcquireDeviceLibrary(const std::string& device_type,
                                                     const platform::fs::path& plugin_path) {
    {
        platform::LockGuard<platform::Mutex> lock(device_plugin_loader_mutex);
        const auto cached = loaded_device_plugins.find(device_type);
        if (cached != loaded_device_plugins.end()) {
            return cached->second;
        }
    }
    if (!platform::fs::exists(plugin_path)) {
        throw std::runtime_error("PluginLoader: Device plugin file not found: " +
                                 plugin_path.string());
    }
    // dlopen MUST 在锁外执行，避免慢插件阻塞其它线程的设备创建。
    auto library = std::make_shared<dylib::library>(plugin_path.string());
    platform::LockGuard<platform::Mutex> lock(device_plugin_loader_mutex);
    const auto cached = loaded_device_plugins.find(device_type);
    if (cached != loaded_device_plugins.end()) {
        return cached->second;
    }
    return loaded_device_plugins.emplace(device_type, std::move(library)).first->second;
}

const DeviceDriverDescriptorV1* ResolveDeviceDriverFromPath(const std::string& device_type,
                                                            const platform::fs::path& plugin_path) {
    const auto library = AcquireDeviceLibrary(device_type, plugin_path);
    const DeviceDriverDescriptorV1* (*factory)() = nullptr;
    try {
        factory = library->get_function<const DeviceDriverDescriptorV1*()>("MakeDeviceDriver");
    } catch (const std::exception&) {
        throw std::runtime_error(
            "PluginLoader: Failed to find MakeDeviceDriver function in device plugin " +
            device_type);
    }
    if (factory == nullptr) {
        throw std::runtime_error(
            "PluginLoader: Failed to find MakeDeviceDriver function in device plugin " +
            device_type);
    }
    const DeviceDriverDescriptorV1* descriptor = factory();
    ValidateDeviceDescriptor(device_type, descriptor);
    return descriptor;
}

const DeviceDriverDescriptorV1* LoadDeviceDriver(const std::string& device_type) {
    return ResolveDeviceDriverFromPath(
        device_type, platform::DeviceLibraryPath(platform::PluginDir(), device_type));
}

}  // namespace

const DeviceDriverDescriptorV1* LookupDeviceDriver(const std::string& device_type) {
    return LoadDeviceDriver(device_type);
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
    // 空信封不触发增量更新，使工厂收到的配置串与既有行为逐字节一致。
    const std::string update_json = extend.empty() ? std::string{} : extend_json;
    auto* adapter = CreateAdapterWithFactory(interface_name, update_json, [=]() {
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
        // 优先使用支持 extend 的入口。新增符号缺失是正常情况，回退到旧入口。
        try {
            auto create_extend = it->second->get_function<CreateAdapterExtendFunc>("MakeAdapterV2");
            if (create_extend != nullptr) {
                return (*create_extend)(interface_name.c_str(), logger_name.c_str(),
                                        LogLevelToInt(log_level), extend_json.c_str());
            }
        } catch (const dylib::symbol_not_found&) {
            // 插件未导出 MakeAdapterV2，走下方回退路径
        }
        auto create_func = it->second->get_function<CreateAdapterFunc>("MakeAdapter");
        if (create_func == nullptr) {
            throw std::runtime_error(
                "PluginLoader: Failed to find MakeAdapter function in plugin " + adapter_type);
        }
        if (!extend.empty()) {
            CreateLogger("PluginLoader", LogLevel::Warn)
                ->warn(
                    "Adapter plugin '{}' does not export MakeAdapterV2; the provided extend "
                    "configuration was ignored",
                    adapter_type);
        }
        return (*create_func)(interface_name.c_str(), logger_name.c_str(),
                              LogLevelToInt(log_level));
    });
    return adapter;
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

std::vector<std::string> GetAvailableDeviceTypes() {
    std::vector<std::string> device_types;
    auto plugin_dir = platform::PluginDir();
    if (!platform::fs::exists(plugin_dir) || !platform::fs::is_directory(plugin_dir)) {
        return device_types;
    }

    // 先无锁收集候选文件，避免在扫描与 dlopen 期间持有全局锁。
    std::vector<std::pair<std::string, platform::fs::path>> candidates;
    for (const auto& entry : platform::fs::directory_iterator(plugin_dir)) {
        if (!platform::fs::is_regular_file(entry.path())) {
            continue;
        }
        std::string filename = entry.path().filename().string();
        std::string device_type;
#ifdef _WIN32
        const std::string suffix = "Device.dll";
        if (filename.size() > suffix.size() &&
            filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
            device_type = filename.substr(0, filename.size() - suffix.size());
        }
#else
        const std::string prefix = "lib";
        const std::string suffix = "Device.so";
        if (filename.size() > prefix.size() + suffix.size() &&
            filename.compare(0, prefix.size(), prefix) == 0 &&
            filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
            device_type =
                filename.substr(prefix.size(), filename.size() - prefix.size() - suffix.size());
        }
#endif
        if (device_type.empty() || IsHiddenDeviceType(device_type)) {
            continue;
        }
        candidates.emplace_back(std::move(device_type), entry.path());
    }

    for (const auto& [device_type, path] : candidates) {
        try {
            ResolveDeviceDriverFromPath(device_type, path);
            device_types.push_back(device_type);
        } catch (const std::exception&) {
            // Skip plugins that cannot be loaded or fail descriptor validation
        }
    }
    std::sort(device_types.begin(), device_types.end());
    return device_types;
}

namespace {

std::map<std::string, std::shared_ptr<dylib::library>> interface_plugins;
platform::Mutex interface_plugin_mutex;

void AppendInterface(const char* name, void* context) {
    if (name != nullptr) {
        static_cast<std::vector<std::string>*>(context)->emplace_back(name);
    }
}

// 每个适配器插件都必须导出 EnumerateInterfaces，由插件自行决定枚举策略。
std::vector<std::string> EnumeratePluginInterfaces(const std::string& adapter_type) {
    const auto plugin_dir = platform::PluginDir();
    const auto plugin_path = platform::PluginLibraryPath(plugin_dir, adapter_type);
    if (!platform::fs::exists(plugin_path)) {
        throw std::runtime_error("PluginLoader: Plugin file not found: " + plugin_path.string());
    }
    std::shared_ptr<dylib::library> library;
    {
        platform::LockGuard<platform::Mutex> lock(interface_plugin_mutex);
        auto it = interface_plugins.find(adapter_type);
        if (it == interface_plugins.end()) {
            it = interface_plugins
                     .emplace(adapter_type, std::make_shared<dylib::library>(plugin_path.string()))
                     .first;
        }
        library = it->second;
    }
    EnumerateAdapterInterfacesFunc* enumerate = nullptr;
    try {
        enumerate = library->get_function<EnumerateAdapterInterfacesFunc>("EnumerateInterfaces");
    } catch (const std::exception&) {
        throw std::runtime_error(
            "PluginLoader: Failed to find EnumerateInterfaces function in "
            "plugin " +
            adapter_type);
    }
    if (enumerate == nullptr) {
        throw std::runtime_error(
            "PluginLoader: Failed to find EnumerateInterfaces function in "
            "plugin " +
            adapter_type);
    }
    std::vector<std::string> interfaces;
    enumerate(&AppendInterface, &interfaces);
    return interfaces;
}

}  // namespace

std::vector<std::string> GetAvailableInterface(const std::string& adapter_type) {
    auto interfaces = EnumeratePluginInterfaces(adapter_type);
    std::sort(interfaces.begin(), interfaces.end());
    return interfaces;
}

void ClearLogger() {
    ClearLoggers();
}

}  // namespace encos
