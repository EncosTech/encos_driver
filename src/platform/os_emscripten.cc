#include "platform/os.h"

namespace encos::platform {

bool GetEnv(const std::string& name, std::string& out) noexcept {
    (void) name;
    out.clear();
    return false;
}

void SetEnv(const std::string& name, const std::string& value) {
    (void) name;
    (void) value;
}

fs::path PluginDir() {
    return {};
}

fs::path PluginLibraryPath(const fs::path& plugin_dir, const std::string& adapter_type) {
    (void) plugin_dir;
    (void) adapter_type;
    return {};
}

std::vector<std::string> GetWiredInterfaceNames() {
    return {};
}

std::vector<std::string> GetIghMasterIds() {
    return {};
}

std::vector<std::string> GetCanInterfaceNames() {
    return {};
}

std::vector<std::string> GetSerialPortNames() {
    return {};
}

}  // namespace encos::platform
