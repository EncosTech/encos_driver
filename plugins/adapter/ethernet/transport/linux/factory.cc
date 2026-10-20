// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "ethernet/transport/linux/xdp.h"
#include "ethernet/transport/transport.h"
#include "platform/os.h"
namespace encos::ethernet {
std::unique_ptr<Transport> CreateTransport(const std::vector<Endpoint>& endpoints, bool multi) {
#ifdef ENCOS_STATIC_MODE
    const std::string broker;
#else
    const auto broker = (platform::PluginDir() / "EthernetXdpBrokerExecutable").string();
#endif
    if (multi)
        return std::make_unique<XdpTransport>(endpoints, broker);
    return std::make_unique<XdpTransport>(endpoints.at(0), broker);
}
void ConfigureHostInterface(const std::string& name) {
#ifdef ENCOS_STATIC_MODE
    ConfigureXdpHostInterface(name, {});
#else
    ConfigureXdpHostInterface(name,
                              (platform::PluginDir() / "EthernetXdpBrokerExecutable").string());
#endif
}
}  // namespace encos::ethernet
