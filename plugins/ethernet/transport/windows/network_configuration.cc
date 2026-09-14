#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Winsock 头必须位于 Windows/IP Helper 头之前。
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
// clang-format on
#include "network_configuration.h"

#include <string>

namespace encos::ethernet {
int ConfigureWindowsInterface(unsigned index) {
    MIB_IFROW row{};
    row.dwIndex = static_cast<DWORD>(index);
    if (GetIfEntry(&row) != NO_ERROR || row.dwType != IF_TYPE_ETHERNET_CSMACD)
        return 3;
    row.dwAdminStatus = MIB_IF_ADMIN_STATUS_UP;
    if (SetIfEntry(&row) != NO_ERROR)
        return 4;
    wchar_t system[MAX_PATH]{};
    const auto length = GetSystemDirectoryW(system, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return 5;
    const auto executable = std::wstring(system) + L"\\netsh.exe";
    auto command =
        L"\"" + executable + L"\" interface ipv4 set address name=" + std::to_wstring(index) +
        L" source=static address=192.168.100.254 mask=255.255.255.0 gateway=none store=persistent";
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        return 6;
    CloseHandle(process.hThread);
    const auto waited = WaitForSingleObject(process.hProcess, 45000);
    DWORD code = 1;
    const bool exited = GetExitCodeProcess(process.hProcess, &code) != 0;
    CloseHandle(process.hProcess);
    if (waited != WAIT_OBJECT_0 || !exited)
        return 7;
    return code == 0 ? 0 : 8;
}

}  // namespace encos::ethernet
