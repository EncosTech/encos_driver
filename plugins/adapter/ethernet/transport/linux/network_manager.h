// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <systemd/sd-bus.h>

namespace encos::ethernet {
enum class NetworkIpv4Method { Unavailable, Manual, Dhcp, Other };

class NetworkManager {
public:
    explicit NetworkManager(const std::string& interface);
    bool Managed() const {
        return !device_.empty();
    }
    NetworkIpv4Method Ipv4Method() const;
    bool AvailableForActivation(NetworkIpv4Method method) const;
    void Activate(const std::string& interface, const std::function<void()>& check_takeover);

private:
    struct CloseBus {
        void operator()(sd_bus* bus) const;
    };
    std::unique_ptr<sd_bus, CloseBus> bus_;
    std::string device_;
};

inline constexpr char kMotorHostAddress[] = "192.168.100.254";
inline constexpr uint32_t kMotorPrefix = 24;
}  // namespace encos::ethernet
