// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <encos/encos_driver.h>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: probe <plugin-dir>\n";
        return 2;
    }
    encos::SetPluginPath(argv[1]);

    const auto types = encos::GetAvailableDeviceTypes();
    std::cout << "available device types:";
    for (const auto& type : types) {
        std::cout << ' ' << type;
    }
    std::cout << "\n";
    if (std::find(types.begin(), types.end(), "Hello") == types.end()) {
        std::cerr << "FAIL: device plugin 'Hello' was not discovered\n";
        return 1;
    }

    auto* adapter = encos::MakeAdapter("Fake", "InstalledPluginProbeAdapter");
    auto* bus = adapter->GetBus(0);
    encos::Device* device = bus->GetDevice("Hello", {{"route_id", 0x5A5}});
    std::cout << "created device: " << device->Type() << " config=" << device->Config() << "\n";

    const bool reused = bus->GetDevice("Hello", {{"route_id", 0x5A5}}) == device;
    const bool distinct = bus->GetDevice("Hello", {{"route_id", 0x5A6}}) != device;
    std::cout << "config identity: reuse=" << reused << " distinct=" << distinct << "\n";

    encos::DeleteAdapter(adapter);
    if (!reused || !distinct) {
        std::cerr << "FAIL: config identity semantics\n";
        return 1;
    }
    std::cout << "OK\n";
    return 0;
}
