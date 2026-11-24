// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "ethernet/transport/linux/host_network.h"

namespace encos::ethernet {
TEST(EthernetHostNetwork, TemporaryAddressOnManagedDhcpDoesNotCountAsStatic) {
    HostNetworkState state;
    state.managed = true;
    state.configured = true;
    state.available_for_activation = true;

    EXPECT_EQ(SelectHostNetworkAction(state), HostNetworkAction::ActivateNetworkManager);
}

TEST(EthernetHostNetwork, UsesExistingStaticAddressWithoutTakingOver) {
    HostNetworkState state;
    state.managed = true;
    state.configured = true;
    state.static_profile = true;

    EXPECT_EQ(SelectHostNetworkAction(state), HostNetworkAction::Reuse);
    state.managed = false;
    EXPECT_EQ(SelectHostNetworkAction(state), HostNetworkAction::Reuse);
}

TEST(EthernetHostNetwork, RequiresAnExplicitlyDedicatedUnmanagedInterface) {
    HostNetworkState state;
    EXPECT_THROW(SelectHostNetworkAction(state), std::runtime_error);
    state.exclusive = true;
    EXPECT_EQ(SelectHostNetworkAction(state), HostNetworkAction::ConfigureDirect);
}

TEST(EthernetHostNetwork, RejectsTakingOverAnInterfaceInUse) {
    HostNetworkState state;
    state.managed = true;
    state.available_for_activation = true;
    state.occupied = true;
    EXPECT_THROW(SelectHostNetworkAction(state), std::runtime_error);
    state.occupied = false;
    state.available_for_activation = false;
    state.exclusive = true;
    EXPECT_THROW(SelectHostNetworkAction(state), std::runtime_error);
}
}  // namespace encos::ethernet
