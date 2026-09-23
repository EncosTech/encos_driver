// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

// clang-format off
#include "socket.h"
// clang-format on
#include <chrono>
#include <filesystem>
#include <shellapi.h>
#include <stdexcept>
#include <thread>

#include "ethernet/transport/transport.h"
#include "platform/os.h"
#ifdef ENCOS_STATIC_MODE
#include "network_configuration.h"
#endif
namespace encos::ethernet {
namespace {
bool Configured(const WindowsInterface& iface) {
    return ntohl(iface.address.s_addr) == 0xc0a864feU && iface.prefix_length == 24 && !iface.dhcp &&
           iface.up;
}
}  // namespace
void ConfigureHostInterface(const std::string& name) {
    unsigned index = 0;
    for (const auto& iface : WindowsInterfaces(true)) {
        if (iface.name != name)
            continue;
        if (Configured(iface))
            return;
        index = iface.index;
    }
    if (!index)
        throw std::invalid_argument("No active IPv4 iface: " + name);
#ifdef ENCOS_STATIC_MODE
    const int code = ConfigureWindowsInterface(index);
    if (code != 0)
        throw std::runtime_error(
            "Static Ethernet network configuration failed; run the final "
            "executable as administrator (error " +
            std::to_string(code) + ")");
#else
    const auto executable =
        std::filesystem::absolute(platform::PluginDir() / "EthernetNetworkHelperExecutable.exe")
            .wstring();
    const auto parameters = std::to_wstring(index);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof info;
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = executable.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info))
        throw std::runtime_error("Ethernet network configuration elevation failed: " +
                                 std::to_string(GetLastError()));
    const auto waited = WaitForSingleObject(info.hProcess, 60000);
    DWORD code = 1;
    const bool exited = GetExitCodeProcess(info.hProcess, &code) != 0;
    CloseHandle(info.hProcess);
    if (waited != WAIT_OBJECT_0 || !exited || code != 0)
        throw std::runtime_error("Ethernet network helper failed or timed out: " +
                                 std::to_string(code));
#endif
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        for (const auto& iface : WindowsInterfaces())
            if (iface.index == index && Configured(iface))
                return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Ethernet static address did not become available");
}
}  // namespace encos::ethernet
