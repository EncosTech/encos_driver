// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "adapter/adapter_extend.h"
#include "can_socket_setup_internal.h"
#include "can_timing.h"
#include "external/json.hpp"

namespace encos::can {

namespace {

constexpr double kTolerance = 0.001;

std::string MakeTargetCanFdOutput() {
    return R"(2: can0: <NOARP,UP,LOWER_UP,ECHO> mtu 72 qdisc fq_codel state UP mode DEFAULT group default qlen 10
    link/can  fd on
    bitrate 1000000 sample-point 0.765
    tq 12.5 prop-seg 5 phase-seg1 6 phase-seg2 2 sjw 1
    dbitrate 5000000 dsample-point 0.882
    dtq 12.5 dprop-seg 5 dphase-seg1 6 dphase-seg2 2 dsjw 1
    clock 40000000
)";
}

std::string MakeClassicCanOutput() {
    return R"(2: can0: <NOARP,UP,LOWER_UP,ECHO> mtu 16 qdisc fq_codel state UP mode DEFAULT group default qlen 10
    link/can
    bitrate 1000000 sample-point 0.750
    tq 12.5 prop-seg 5 phase-seg1 6 phase-seg2 2 sjw 1
    clock 40000000
)";
}

}  // namespace

TEST(CanSocketSetupTests, ParsesCanFdTimingFields) {
    const auto config = ParseCanDetails(MakeTargetCanFdOutput());
    EXPECT_TRUE(config.up);
    EXPECT_TRUE(config.fd_on);
    EXPECT_EQ(config.bitrate, 1000000);
    EXPECT_NEAR(config.sample_point, 0.765, kTolerance);
    EXPECT_EQ(config.dbitrate, 5000000);
    EXPECT_NEAR(config.dsample_point, 0.882, kTolerance);
}

TEST(CanSocketSetupTests, ParsesClassicCanTimingFields) {
    const auto config = ParseCanDetails(MakeClassicCanOutput());
    EXPECT_TRUE(config.up);
    EXPECT_FALSE(config.fd_on);
    EXPECT_EQ(config.bitrate, 1000000);
    EXPECT_NEAR(config.sample_point, 0.750, kTolerance);
    EXPECT_EQ(config.dbitrate, 0);
    EXPECT_NEAR(config.dsample_point, 0.0, kTolerance);
}

TEST(CanSocketSetupTests, TargetConfigMatchesItself) {
    const auto config = ParseCanDetails(MakeTargetCanFdOutput());
    EXPECT_TRUE(IsCanConfigMatchingTarget(config, CanTiming{}));
}

TEST(CanSocketSetupTests, DownStateRequiresReconfiguration) {
    auto output = MakeTargetCanFdOutput();
    const auto pos = output.find("state UP");
    ASSERT_NE(pos, std::string::npos);
    output.replace(pos, 8, "state DOWN");
    const auto config = ParseCanDetails(output);
    EXPECT_FALSE(config.up);
    EXPECT_FALSE(IsCanConfigMatchingTarget(config, CanTiming{}));
}

TEST(CanSocketSetupTests, MissingFdRequiresReconfiguration) {
    const auto config = ParseCanDetails(MakeClassicCanOutput());
    EXPECT_FALSE(IsCanConfigMatchingTarget(config, CanTiming{}));
}

TEST(CanSocketSetupTests, WrongBitrateRequiresReconfiguration) {
    auto output = MakeTargetCanFdOutput();
    const auto pos = output.find("bitrate 1000000");
    ASSERT_NE(pos, std::string::npos);
    output.replace(pos, 15, "bitrate 500000");
    const auto config = ParseCanDetails(output);
    EXPECT_EQ(config.bitrate, 500000);
    EXPECT_FALSE(IsCanConfigMatchingTarget(config, CanTiming{}));
}

TEST(CanSocketSetupTests, WrongSamplePointRequiresReconfiguration) {
    auto output = MakeTargetCanFdOutput();
    const auto pos = output.find("sample-point 0.765");
    ASSERT_NE(pos, std::string::npos);
    output.replace(pos, 18, "sample-point 0.900");
    const auto config = ParseCanDetails(output);
    EXPECT_NEAR(config.sample_point, 0.900, kTolerance);
    EXPECT_FALSE(IsCanConfigMatchingTarget(config, CanTiming{}));
}

TEST(CanSocketSetupTests, WrongDbitrateRequiresReconfiguration) {
    auto output = MakeTargetCanFdOutput();
    const auto pos = output.find("dbitrate 5000000");
    ASSERT_NE(pos, std::string::npos);
    output.replace(pos, 16, "dbitrate 4000000");
    const auto config = ParseCanDetails(output);
    EXPECT_EQ(config.dbitrate, 4000000);
    EXPECT_FALSE(IsCanConfigMatchingTarget(config, CanTiming{}));
}

TEST(CanSocketSetupTests, WrongDsamplePointRequiresReconfiguration) {
    auto output = MakeTargetCanFdOutput();
    const auto pos = output.find("dsample-point 0.882");
    ASSERT_NE(pos, std::string::npos);
    output.replace(pos, 19, "dsample-point 0.990");
    const auto config = ParseCanDetails(output);
    EXPECT_NEAR(config.dsample_point, 0.990, kTolerance);
    EXPECT_FALSE(IsCanConfigMatchingTarget(config, CanTiming{}));
}

TEST(CanSocketSetupTests, SetupCommandIncludesTargetTiming) {
    const auto args = BuildCanSetupCommandArgs("can0", CanTiming{});
    const std::vector<std::string> expected{
        "ip",       "link",    "set",           "can0",     "type", "can",
        "bitrate",  "1000000", "sample-point",  "0.765000", "fd",   "on",
        "dbitrate", "5000000", "dsample-point", "0.882000"};
    ASSERT_EQ(args.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (i == 13 || i == 19) {
            // sample-point / dsample-point: allow small numeric equivalence
            EXPECT_NEAR(std::stod(args[i]), std::stod(expected[i]), kTolerance) << "at index " << i;
        } else {
            EXPECT_EQ(args[i], expected[i]) << "at index " << i;
        }
    }
}

TEST(CanSocketSetupTests, UpAndDownCommandsAreFormatted) {
    EXPECT_EQ(BuildCanUpCommandArgs("can0"),
              (std::vector<std::string>{"ip", "link", "set", "can0", "up"}));
    EXPECT_EQ(BuildCanDownCommandArgs("can0"),
              (std::vector<std::string>{"ip", "link", "set", "can0", "down"}));
}

}  // namespace encos::can

namespace encos::can {
TEST(CanSocketSetupTests, ParsesGsUsbKernelInterfaceOutput) {
    const auto config = ParseCanDetails(
        R"(78: can0: <NOARP,UP,LOWER_UP,ECHO> mtu 72 qdisc pfifo_fast state UNKNOWN mode DEFAULT
    link/can
    can <FD> state ERROR-ACTIVE (berr-counter tx 0 rx 0) restart-ms 0
      bitrate 1000000 sample-point 0.750
      dbitrate 5000000 dsample-point 0.875
)");
    EXPECT_TRUE(config.up);
    EXPECT_TRUE(config.fd_on);
    EXPECT_TRUE(IsCanConfigMatchingTarget(config, CanTiming{}));
}
}  // namespace encos::can

namespace encos::can {

TEST(CanSocketSetupTests, CustomTimingDrivesSetupCommand) {
    CanTiming timing;
    timing.bitrate = 500000;
    timing.sample_point = 0.875;
    const auto args = BuildCanSetupCommandArgs("can0", timing);
    ASSERT_EQ(args.size(), 16u);
    EXPECT_EQ(args[7], "500000");
    EXPECT_NEAR(std::stod(args[9]), 0.875, kTolerance);
    // 未覆盖的字段沿用默认值
    EXPECT_EQ(args[13], "5000000");
    EXPECT_NEAR(std::stod(args[15]), 0.882, kTolerance);
}

TEST(CanSocketSetupTests, MatchingComparesAgainstEffectiveTiming) {
    auto output = MakeTargetCanFdOutput();
    const auto pos = output.find("bitrate 1000000");
    output.replace(pos, 15, "bitrate 500000");
    const auto config = ParseCanDetails(output);

    EXPECT_FALSE(IsCanConfigMatchingTarget(config, CanTiming{}));

    CanTiming custom;
    custom.bitrate = 500000;
    EXPECT_TRUE(IsCanConfigMatchingTarget(config, custom));
}

namespace {

/** @brief 便捷：把信封文本应用到一条总线上的默认时序 */
CanTiming ApplyFromEnvelope(const std::string& text, int slave = 0, int bus = 0) {
    const auto extend = AdapterExtend::Parse(text);
    CanTiming timing;
    ApplyCanTiming(extend.Scope(slave, bus), &timing);
    return timing;
}

}  // namespace

TEST(CanTimingDecodeTests, EmptyEnvelopeAndMissingFieldsShareDefaults) {
    const CanTiming defaults;

    const auto from_empty = ApplyFromEnvelope("");
    EXPECT_EQ(from_empty.bitrate, defaults.bitrate);
    EXPECT_DOUBLE_EQ(from_empty.sample_point, defaults.sample_point);
    EXPECT_EQ(from_empty.dbitrate, defaults.dbitrate);
    EXPECT_DOUBLE_EQ(from_empty.dsample_point, defaults.dsample_point);

    const auto partial = ApplyFromEnvelope(R"({"bitrate": 500000})");
    EXPECT_EQ(partial.bitrate, 500000);
    EXPECT_DOUBLE_EQ(partial.sample_point, defaults.sample_point);
    EXPECT_EQ(partial.dbitrate, defaults.dbitrate);
    EXPECT_DOUBLE_EQ(partial.dsample_point, defaults.dsample_point);
}

TEST(CanTimingDecodeTests, PerBusOverrideAppliesToThatBusOnly) {
    const std::string text =
        R"({"bitrate": 1000000, "buses": {"0": {"bitrate": 250000}, "0:0": {}}})";
    // 两种拼写指向同一总线，信封层直接拒绝
    EXPECT_THROW(AdapterExtend::Parse(text), std::runtime_error);

    const std::string valid = R"({"bitrate": 1000000, "buses": {"0": {"bitrate": 250000}}})";
    EXPECT_EQ(ApplyFromEnvelope(valid, 0, 0).bitrate, 250000);
}

TEST(CanTimingDecodeTests, RejectsInvalidValues) {
    EXPECT_THROW(ApplyFromEnvelope(R"({"bitrate": -1})"), std::runtime_error);
    EXPECT_THROW(ApplyFromEnvelope(R"({"bitrate": 1.5})"), std::runtime_error);
    EXPECT_THROW(ApplyFromEnvelope(R"({"dbitrate": 0})"), std::runtime_error);
    EXPECT_THROW(ApplyFromEnvelope(R"({"sample_point": 1.5})"), std::runtime_error);
    EXPECT_THROW(ApplyFromEnvelope(R"({"sample_point": 0.0})"), std::runtime_error);
    EXPECT_THROW(ApplyFromEnvelope(R"({"sample_point": "fast"})"), std::runtime_error);
    EXPECT_THROW(ApplyFromEnvelope(R"({"dsample_point": 2})"), std::runtime_error);
}

TEST(CanTimingDecodeTests, RejectsUnknownFields) {
    EXPECT_THROW(ApplyFromEnvelope(R"({"bitrat": 500000})"), std::runtime_error);
    EXPECT_THROW(ApplyFromEnvelope(R"({"buses": {"0": {"samplepoint": 0.8}}})"),
                 std::runtime_error);
}

TEST(CanTimingDecodeTests, ScopingErrorsComeFromTheEnvelope) {
    // 单总线校验在 CreateWithExtend 中完成；此处确认信封能正确暴露被声明的总线
    const auto extend = AdapterExtend::Parse(R"({"buses": {"1:0": {"bitrate": 500000}}})");
    ASSERT_EQ(extend.DeclaredBuses().size(), 1u);
    EXPECT_EQ(extend.DeclaredBuses()[0], std::make_pair(1, 0));
    // 该总线的覆盖不会泄漏到 (0, 0)
    EXPECT_TRUE(extend.Scope(0, 0).empty());
}

TEST(CanTimingCodecTests, RoundTripsThroughTheBrokerArgument) {
    CanTiming timing;
    timing.bitrate = 500000;
    timing.sample_point = 0.875;
    timing.dbitrate = 2000000;
    timing.dsample_point = 0.75;

    CanTiming parsed;
    ASSERT_TRUE(ParseCanTiming(FormatCanTiming(timing), &parsed));
    EXPECT_EQ(parsed.bitrate, timing.bitrate);
    EXPECT_DOUBLE_EQ(parsed.sample_point, timing.sample_point);
    EXPECT_EQ(parsed.dbitrate, timing.dbitrate);
    EXPECT_DOUBLE_EQ(parsed.dsample_point, timing.dsample_point);
}

TEST(CanTimingCodecTests, RejectsMalformedBrokerArguments) {
    CanTiming timing;
    EXPECT_FALSE(ParseCanTiming("", &timing));
    EXPECT_FALSE(ParseCanTiming("1000000,0.765,5000000", &timing));
    EXPECT_FALSE(ParseCanTiming("1000000,0.765,5000000,0.882,extra", &timing));
    EXPECT_FALSE(ParseCanTiming("abc,0.765,5000000,0.882", &timing));
    EXPECT_FALSE(ParseCanTiming("0,0.765,5000000,0.882", &timing));
    EXPECT_FALSE(ParseCanTiming("1000000,1.5,5000000,0.882", &timing));
}

}  // namespace encos::can
