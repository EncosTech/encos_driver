// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "platform/os.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define WIN32_LEAN_AND_MEAN
// Winsock must precede Windows/IP Helper headers.
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
// clang-format on
#include <cstdlib>
#include <mmsystem.h>

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

// Windows 默认定时器精度约 15.6ms，std::this_thread::sleep_until 无法稳定 1ms；
// 将系统定时器精度提升到 1ms，使 EtherCAT 循环可保持 1000Hz。
struct TimerResolutionGuard {
    TimerResolutionGuard() {
        timeBeginPeriod(1);
    }
    ~TimerResolutionGuard() {
        timeEndPeriod(1);
    }
};

const TimerResolutionGuard kTimerResolutionGuard;

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
    std::string result(size_needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, &result[0], size_needed, nullptr, nullptr);
    result.pop_back();
    return result;
}

}  // namespace

bool GetEnv(const std::string& name, std::string& out) noexcept {
#if defined(_MSC_VER) || defined(_UCRT)
    char* value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name.c_str()) != 0 || value == nullptr) {
        return false;
    }
    out = value;
    free(value);
    return true;
#else
    // msvcrt 目标的 MinGW（如 w64devkit）没有 _dupenv_s
    const char* value = std::getenv(name.c_str());
    if (value == nullptr) {
        return false;
    }
    out = value;
    return true;
#endif
}

void SetEnv(const std::string& name, const std::string& value) {
#if defined(_MSC_VER) || defined(_UCRT)
    _putenv_s(name.c_str(), value.c_str());
#else
    // msvcrt 目标的 MinGW 没有 _putenv_s；必须用 _putenv 以同步 CRT 的 getenv 视图，
    // 否则 SetEnvironmentVariableA 更新的是 Win32 环境块，getenv 读不到。
    _putenv((name + "=" + value).c_str());
#endif
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

fs::path DeviceLibraryPath(const fs::path& plugin_dir, const std::string& device_type) {
    return plugin_dir / (device_type + "Device.dll");
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
    // 设备列表长度随系统变化，固定缓冲区会因 ERROR_INSUFFICIENT_BUFFER 返回空列表；
    // 这里在缓冲区不足时逐步扩容。
    std::vector<char> buffer(4096);
    for (;;) {
        const DWORD length =
            QueryDosDeviceA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length != 0) {
            const char* current = buffer.data();
            while (*current != '\0') {
                std::string name(current);
                if (StartsWith(name, "COM")) {
                    ports.push_back(name);
                }
                current += name.length() + 1;
            }
            break;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer.size() >= (1U << 20)) {
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return ports;
}

}  // namespace encos::platform
