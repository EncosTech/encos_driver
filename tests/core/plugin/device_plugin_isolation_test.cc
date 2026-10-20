// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <gtest/gtest.h>

#include "test_claim_device.h"
#include "test_device.h"

namespace encos {
namespace {

TEST(DevicePluginIsolationTest, FactoriesKeepIndependentDescriptorsAndRoutes) {
    const auto* first = MakeTestDeviceDriver();
    const auto* second = MakeTestClaimDeviceDriver();
    EXPECT_NE(first, second);
    EXPECT_STREQ(first->type_name, "TestDevice");
    EXPECT_STREQ(second->type_name, "TestClaimDevice");
    std::uint32_t route = 0;
    ASSERT_EQ(first->fill_route_ids(R"({"base_id": 1234})", &route, 1), 1u);
    EXPECT_EQ(route, 1234u);
    ASSERT_EQ(second->fill_route_ids("{}", &route, 1), 1u);
    EXPECT_EQ(route, kDeviceClaimBusRouteId);
    EXPECT_NE(first->create, second->create);
    EXPECT_NE(first->destroy, second->destroy);
    EXPECT_EQ(MakeTestDeviceDriver(), first);
}

}  // namespace
}  // namespace encos
