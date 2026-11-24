// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
#include <dlfcn.h>
#include <encos/adapter/fake_adapter_control.h>
#include <encos/encos_driver.h>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) {
            throw std::runtime_error(
                "Expected plugin directory and optional comma-separated adapter types");
        }
        const auto plugins = std::filesystem::canonical(argv[1]);
        for (const auto& entry : std::filesystem::directory_iterator(plugins)) {
            if (entry.path().extension() == ".so") {
                void* library = dlopen(entry.path().c_str(), RTLD_NOW | RTLD_LOCAL);
                if (library == nullptr) {
                    throw std::runtime_error(dlerror());
                }
                dlclose(library);
            }
        }
        encos::SetPluginPath(plugins.string());
        std::set<std::string> expected{"Can", "Ethercat", "UsbSerial", "Slcan", "RelayWs"};
        if (argc == 3) {
            std::istringstream optional(argv[2]);
            std::string name;
            while (std::getline(optional, name, ',')) {
                if (!name.empty()) {
                    expected.insert(name);
                }
            }
        }
        const auto available = encos::GetAvailableAdapterTypes();
        if (std::set<std::string>(available.begin(), available.end()) != expected) {
            throw std::runtime_error("Installed adapter set does not match selected packages");
        }
        auto* adapter = encos::MakeAdapter("Fake", "portable-smoke");
        if (adapter == nullptr || adapter->GetFakeAdapterControl() == nullptr) {
            throw std::runtime_error("Fake adapter unavailable");
        }
        adapter->GetFakeAdapterControl()->EnableAutoCreateMotor();
        auto* motor = adapter->GetBus(0)->GetMotor(1, encos::MotorModel::EC_A4310_P2);
        if (motor == nullptr) {
            throw std::runtime_error("Fake motor unavailable");
        }
        motor->PVTControl<0>(0, 1, 0, 0, 0);
        (void) motor->GetParameter<encos::MotorParameter::Position>();
        encos::DeleteAdapter(adapter);
        std::cout << "PASS: all plugins loaded; Fake motor PVT and Position query\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
