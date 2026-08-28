#include "platform/os.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define WIN32_LEAN_AND_MEAN
#include <cstdlib>
#include <iphlpapi.h>
#include <windows.h>
#include <winsock2.h>

#ifndef GAA_FLAG_SKIP_ANYCAST
#define GAA_FLAG_SKIP_ANYCAST 0x2
#endif
#ifndef GAA_FLAG_SKIP_MULTICAST
#define GAA_FLAG_SKIP_MULTICAST 0x4
#endif
#ifndef GAA_FLAG_SKIP_DNS_SERVER
#define GAA_FLAG_SKIP_DNS_SERVER 0x8
#endif

namespace encos::platform {

namespace {

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string Utf16ToUtf8(const wchar_t* value) {
    if (!value) {
        return {};
    }
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) {
        return {};
    }
    std::string result(size_needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, &result[0], size_needed, nullptr, nullptr);
    return result;
}

}  // namespace

bool GetEnv(const std::string& name, std::string& out) noexcept {
    char* value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name.c_str()) != 0 || value == nullptr) {
        return false;
    }
    out = value;
    free(value);
    return true;
}

void SetEnv(const std::string& name, const std::string& value) {
    _putenv_s(name.c_str(), value.c_str());
}

fs::path PluginDir() {
    std::string dir;
    if (GetEnv("ENCOS_PLUGIN_PATH", dir)) {
        return fs::path(dir);
    }
    return fs::absolute("./plugins");
}

fs::path PluginLibraryPath(const fs::path& plugin_dir, const std::string& adapter_type) {
    return plugin_dir / (adapter_type + "Plugin.dll");
}

std::vector<std::string> GetWiredInterfaceNames() {
    std::vector<std::string> result;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG out_buf_len = 0;
    PIP_ADAPTER_ADDRESSES adapters = nullptr;
    ULONG ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &out_buf_len);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        adapters = static_cast<PIP_ADAPTER_ADDRESSES>(malloc(out_buf_len));
        if (!adapters) {
            return result;
        }
        ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &out_buf_len);
    }
    if (ret == NO_ERROR && adapters) {
        for (PIP_ADAPTER_ADDRESSES adapter = adapters; adapter; adapter = adapter->Next) {
            if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
                adapter->IfType == IF_TYPE_IEEE80211 || adapter->OperStatus != IfOperStatusUp) {
                continue;
            }
            std::string name = Utf16ToUtf8(adapter->FriendlyName);
            if (name.empty()) {
                name = adapter->AdapterName ? adapter->AdapterName : std::string();
            }
            if (!name.empty() && !StartsWith(name, "can")) {
                result.push_back(name);
            }
        }
    }
    if (adapters) {
        free(adapters);
    }
    return result;
}

std::vector<std::string> GetIghMasterIds() {
    return {};
}

std::vector<std::string> GetCanInterfaceNames() {
    return {};
}

std::vector<std::string> GetSerialPortNames() {
    std::vector<std::string> ports;
    char target_path[5000];
    if (QueryDosDeviceA(nullptr, target_path, sizeof(target_path))) {
        char* current = target_path;
        while (*current) {
            std::string name(current);
            if (StartsWith(name, "COM")) {
                ports.push_back(name);
            }
            current += name.length() + 1;
        }
    }
    return ports;
}

}  // namespace encos::platform
