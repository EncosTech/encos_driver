// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <stdexcept>

namespace encos::ethernet {
enum class HostNetworkAction { Reuse, ActivateNetworkManager, ConfigureDirect };

struct HostNetworkState {
    bool managed = false;
    bool static_profile = false;
    bool configured = false;
    bool available_for_activation = false;
    bool occupied = false;
    bool exclusive = false;
};

HostNetworkAction SelectHostNetworkAction(const HostNetworkState& state);
}  // namespace encos::ethernet
