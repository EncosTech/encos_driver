#include "net_interface.h"

#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define WIN32_LEAN_AND_MEAN
#include <iphlpapi.h>
#include <windows.h>
#include <winsock2.h>

// 兼容部分编译器未定义的宏
#ifndef GAA_FLAG_SKIP_ANYCAST
#define GAA_FLAG_SKIP_ANYCAST 0x2
#endif
#ifndef GAA_FLAG_SKIP_MULTICAST
#define GAA_FLAG_SKIP_MULTICAST 0x4
#endif
#ifndef GAA_FLAG_SKIP_DNS_SERVER
#define GAA_FLAG_SKIP_DNS_SERVER 0x8
#endif

namespace {
std::string Utf16ToUtf8(const wchar_t* wstr) {
    if (!wstr)
        return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (size_needed <= 0)
        return {};
    std::string str_to(size_needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str_to[0], size_needed, NULL, NULL);
    return str_to;
}
}  // namespace

std::vector<std::string> encos::GetWiredInterfaceNames() {
    std::vector<std::string> res;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG family = AF_UNSPEC;
    ULONG out_buf_len = 0;
    PIP_ADAPTER_ADDRESSES adapters = nullptr;
    ULONG ret = GetAdaptersAddresses(family, flags, NULL, adapters, &out_buf_len);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        adapters = (PIP_ADAPTER_ADDRESSES) malloc(out_buf_len);
        if (!adapters)
            return res;
        ret = GetAdaptersAddresses(family, flags, NULL, adapters, &out_buf_len);
    }
    if (ret == NO_ERROR && adapters) {
        for (PIP_ADAPTER_ADDRESSES a = adapters; a; a = a->Next) {
            if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;
            if (a->IfType == IF_TYPE_IEEE80211)
                continue;  // wifi
            if (a->OperStatus != IfOperStatusUp)
                continue;
            // prefer FriendlyName if available
            std::string name = Utf16ToUtf8(a->FriendlyName);
            if (name.empty())
                name = a->AdapterName ? a->AdapterName : std::string();
            if (name.empty())
                continue;
            // skip obvious CAN or virtual names
            if (StartsWith(name, "can"))
                continue;
            res.push_back(name);
        }
    }
    if (adapters)
        free(adapters);
    return res;
}

#else  // POSIX / Linux

#include <fstream>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <cstring>
#include <dirent.h>
#endif

std::vector<std::string> encos::GetWiredInterfaceNames() {
    std::set<std::string> names;
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) == 0) {
        for (struct ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_name)
                continue;
            names.insert(std::string(ifa->ifa_name));
        }
        freeifaddrs(ifap);
    }

    std::vector<std::string> res;
    for (const auto& name : names) {
        if (name == "lo")
            continue;
        if (StartsWith(name, "lo"))
            continue;
        if (StartsWith(name, "can"))
            continue;
        if (StartsWith(name, "wl"))
            continue;  // common wireless prefix

        // skip if /sys/class/net/<if>/wireless exists -> wifi
        std::string wireless_path = std::string("/sys/class/net/") + name + "/wireless";
        struct stat st;
        if (stat(wireless_path.c_str(), &st) == 0)
            continue;

        // check operstate
        std::string operstate_path = std::string("/sys/class/net/") + name + "/operstate";
        std::ifstream f(operstate_path);
        std::string state;
        if (!f || !(f >> state))
            continue;
        if (state != "up")
            continue;

        res.push_back(name);
    }
    return res;
}

std::vector<std::string> encos::GetCanInterfaceNames() {
    std::vector<std::string> res;
    const char* sys_net = "/sys/class/net";
    DIR* d = opendir(sys_net);
    if (!d)
        return res;
    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        if (!de->d_name)
            continue;
        if (de->d_name[0] == '.')
            continue;
        std::string name(de->d_name);

        // Read the type file; CAN devices have ARPHRD_CAN (usually 280)
        std::string type_path = std::string(sys_net) + "/" + name + "/type";
        std::ifstream f(type_path);
        if (!f)
            continue;
        int type = 0;
        if (!(f >> type))
            continue;
        const int ARPHRD_CAN = 280;
        if (type == ARPHRD_CAN)
            res.push_back(name);
    }
    closedir(d);
    return res;
}

#endif
