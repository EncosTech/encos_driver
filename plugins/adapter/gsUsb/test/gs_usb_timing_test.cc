// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "gs_usb_timing.h"

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

#include "adapter/adapter_extend.h"

namespace encos::gs_usb {
namespace {

TEST(GsUsbTimingTests, EmptyExtendKeepsPluginDefaults) {
    const auto extend = AdapterExtend::Parse("");
    const auto timings = BuildChannelTimings(extend, 8);
    ASSERT_EQ(timings.size(), 8U);
    for (const auto& timing : timings) {
        EXPECT_EQ(timing.nominal_bps, 1000000U);
        EXPECT_EQ(timing.data_bps, 5000000U);
        EXPECT_DOUBLE_EQ(timing.nominal_sample_point, 0.765);
        EXPECT_DOUBLE_EQ(timing.data_sample_point, 0.882);
    }
}

TEST(GsUsbTimingTests, CommonFieldsApplyToEveryChannel) {
    const auto extend = AdapterExtend::Parse(R"({"bitrate": 500000, "sample_point": 0.875})");
    const auto timings = BuildChannelTimings(extend, 8);
    for (const auto& timing : timings) {
        EXPECT_EQ(timing.nominal_bps, 500000U);
        EXPECT_DOUBLE_EQ(timing.nominal_sample_point, 0.875);
        EXPECT_EQ(timing.data_bps, 5000000U);
        EXPECT_DOUBLE_EQ(timing.data_sample_point, 0.882);
    }
}

TEST(GsUsbTimingTests, PerChannelOverrideOnlyTouchesThatChannel) {
    const auto extend = AdapterExtend::Parse(
        R"({"bitrate": 1000000, "buses": {"0:6": {"bitrate": 250000}, "7": {"bitrate": 500000}}})");
    const auto timings = BuildChannelTimings(extend, 8);
    EXPECT_EQ(timings[6].nominal_bps, 250000U);
    EXPECT_EQ(timings[7].nominal_bps, 500000U);
    for (unsigned channel = 0; channel < 8; ++channel) {
        if (channel != 6U && channel != 7U) {
            EXPECT_EQ(timings[channel].nominal_bps, 1000000U);
        }
    }
}

TEST(GsUsbTimingTests, RejectsUnknownField) {
    const auto extend = AdapterExtend::Parse(R"({"baudrate": 500000})");
    EXPECT_THROW(BuildChannelTimings(extend, 8), std::runtime_error);
}

TEST(GsUsbTimingTests, RejectsInvalidValues) {
    EXPECT_THROW(BuildChannelTimings(AdapterExtend::Parse(R"({"bitrate": -1})"), 8),
                 std::runtime_error);
    EXPECT_THROW(BuildChannelTimings(AdapterExtend::Parse(R"({"sample_point": 1.5})"), 8),
                 std::runtime_error);
    EXPECT_THROW(BuildChannelTimings(AdapterExtend::Parse(R"({"dbitrate": 0})"), 8),
                 std::runtime_error);
}

}  // namespace
}  // namespace encos::gs_usb
