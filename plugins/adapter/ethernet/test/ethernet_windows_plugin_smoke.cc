// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "encos/encos_driver.h"
#include "platform/os.h"
int main() {
    auto expected = encos::platform::GetWiredInterfaceNames();
    std::sort(expected.begin(), expected.end());
    const auto plugin_directory = encos::platform::PluginDir().string();
    // 接口枚举由插件 ABI 提供：插件目录缺失时必须报错而不是回退到平台枚举。
    encos::SetPluginPath("./__missing_plugin_directory__");
    bool missing_plugin_rejected = false;
    try {
        (void) encos::GetAvailableInterface("Ethernet");
    } catch (const std::exception&) {
        missing_plugin_rejected = true;
    }
    if (!missing_plugin_rejected) {
        return 1;
    }
    encos::SetPluginPath(plugin_directory);
    if (encos::GetAvailableInterface("Ethernet") != expected) {
        return 2;
    }
    const auto types = encos::GetAvailableAdapterTypes();
    if (std::find(types.begin(), types.end(), "Ethernet") == types.end()) {
        return 3;
    }
    try {
        (void) encos::MakeAdapter("Ethernet", "__encos_missing_interface__");
        return 4;
    } catch (const std::invalid_argument& error) {
        if (std::string(error.what()).find("No active IPv4") == std::string::npos) {
            return 5;
        }
    }
    // The process must exit without explicitly shutting down the default logger.
    std::cout << "Ethernet DLL enumeration, factory rollback and logger exit passed" << std::endl;
    return 0;
}
