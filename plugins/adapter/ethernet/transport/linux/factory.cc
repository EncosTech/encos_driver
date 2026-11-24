// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "ethernet/transport/linux/xdp.h"
#include "ethernet/transport/transport.h"
#include "platform/os.h"
namespace encos::ethernet {
std::unique_ptr<HostInterface> PrepareLinuxHostInterface(const std::string& name,
                                                         const std::string& broker, bool exclusive);
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
std::unique_ptr<HostInterface> ConfigureHostInterface(const std::string& name, bool exclusive) {
#ifdef ENCOS_STATIC_MODE
    return PrepareLinuxHostInterface(name, {}, exclusive);
#else
    return PrepareLinuxHostInterface(
        name, (platform::PluginDir() / "EthernetXdpBrokerExecutable").string(), exclusive);
#endif
}
}  // namespace encos::ethernet
