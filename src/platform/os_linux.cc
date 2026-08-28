#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <ifaddrs.h>
#include <net/if.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

#include "platform/os.h"

#ifndef ENCOS_PLUGIN_INSTALL_PATH
#define ENCOS_PLUGIN_INSTALL_PATH "/usr/local/lib/encosPlugins"
#endif

namespace encos::platform {

namespace {

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

}  // namespace

bool GetEnv(const std::string& name, std::string& out) noexcept {
    const char* value = std::getenv(name.c_str());
    if (!value) {
        return false;
    }
    out = value;
    return true;
}

void SetEnv(const std::string& name, const std::string& value) {
    setenv(name.c_str(), value.c_str(), 1);
}

fs::path PluginDir() {
    std::string dir;
    if (GetEnv("ENCOS_PLUGIN_PATH", dir)) {
        return fs::path(dir);
    }
    return ENCOS_PLUGIN_INSTALL_PATH;
}

fs::path PluginLibraryPath(const fs::path& plugin_dir, const std::string& adapter_type) {
    return plugin_dir / ("lib" + adapter_type + "Plugin.so");
}

std::vector<std::string> GetWiredInterfaceNames() {
    std::set<std::string> names;
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) == 0) {
        for (struct ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_name) {
                names.insert(std::string(ifa->ifa_name));
            }
        }
        freeifaddrs(ifap);
    }

    std::vector<std::string> result;
    for (const auto& name : names) {
        if (name == "lo" || StartsWith(name, "lo") || StartsWith(name, "can") ||
            StartsWith(name, "wl")) {
            continue;
        }

        std::string wireless_path = std::string("/sys/class/net/") + name + "/wireless";
        struct stat st;
        if (stat(wireless_path.c_str(), &st) == 0) {
            continue;
        }

        std::string operstate_path = std::string("/sys/class/net/") + name + "/operstate";
        std::ifstream state_file(operstate_path);
        std::string state;
        if (!state_file || !(state_file >> state) || state != "up") {
            continue;
        }

        result.push_back(name);
    }
    return result;
}

std::vector<std::string> GetIghMasterIds() {
    std::vector<unsigned int> master_ids;
    DIR* dir = opendir("/dev");
    if (!dir) {
        return {};
    }

    constexpr std::string_view kPrefix = "EtherCAT";
    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name(entry->d_name);
        if (name.size() <= kPrefix.size() || name.compare(0, kPrefix.size(), kPrefix) != 0 ||
            !std::all_of(name.begin() + static_cast<std::ptrdiff_t>(kPrefix.size()), name.end(),
                         [](unsigned char character) {
                             return std::isdigit(character) != 0;
                         })) {
            continue;
        }

        struct stat status {};
        const std::string path = std::string("/dev/") + name;
        if (stat(path.c_str(), &status) != 0 || !S_ISCHR(status.st_mode)) {
            continue;
        }

        try {
            master_ids.push_back(
                static_cast<unsigned int>(std::stoul(name.substr(kPrefix.size()))));
        } catch (const std::exception&) {
            continue;
        }
    }
    closedir(dir);

    std::sort(master_ids.begin(), master_ids.end());
    master_ids.erase(std::unique(master_ids.begin(), master_ids.end()), master_ids.end());

    std::vector<std::string> result;
    result.reserve(master_ids.size());
    for (const auto master_id : master_ids) {
        result.push_back(std::to_string(master_id));
    }
    return result;
}

std::vector<std::string> GetCanInterfaceNames() {
    std::vector<std::string> result;
    const char* sys_net = "/sys/class/net";
    DIR* dir = opendir(sys_net);
    if (!dir) {
        return result;
    }

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (!entry->d_name || entry->d_name[0] == '.') {
            continue;
        }
        std::string name(entry->d_name);
        std::ifstream type_file(std::string(sys_net) + "/" + name + "/type");
        int type = 0;
        if (type_file && (type_file >> type) && type == 280) {
            result.push_back(name);
        }
    }
    closedir(dir);
    return result;
}

std::vector<std::string> GetSerialPortNames() {
    std::vector<std::string> ports;
    const fs::path tty_dir = "/sys/class/tty";
    if (!fs::exists(tty_dir)) {
        return ports;
    }

    for (const auto& entry : fs::directory_iterator(tty_dir)) {
        std::string name = entry.path().filename().string();
        if (!StartsWith(name, "ttyUSB") && !StartsWith(name, "ttyACM")) {
            continue;
        }
        fs::path device_link = entry.path() / "device";
        if (fs::exists(device_link) && fs::is_symlink(device_link)) {
            ports.push_back("/dev/" + name);
        }
    }
    return ports;
}

}  // namespace encos::platform
