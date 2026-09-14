#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "encos/encos_driver.h"
#include "platform/os.h"
int main() {
    auto expected = encos::platform::GetWiredInterfaceNames();
    std::sort(expected.begin(), expected.end());
    const auto plugin_directory = encos::platform::PluginDir().string();
    encos::SetPluginPath("./__missing_plugin_directory__");
    if (encos::GetAvailableInterface("Ethernet") != expected)
        return 1;
    encos::SetPluginPath(plugin_directory);
    const auto types = encos::GetAvailableAdapterTypes();
    if (std::find(types.begin(), types.end(), "Ethernet") == types.end())
        return 2;
    try {
        (void) encos::MakeAdapter("Ethernet", "__encos_missing_interface__");
        return 3;
    } catch (const std::invalid_argument& error) {
        if (std::string(error.what()).find("No active IPv4") == std::string::npos)
            return 4;
    }
    // The process must exit without explicitly shutting down the default logger.
    std::cout << "Ethernet DLL enumeration, factory rollback and logger exit passed" << std::endl;
    return 0;
}
