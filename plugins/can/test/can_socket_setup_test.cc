#include <gtest/gtest.h>

#include "can_socket_setup_internal.h"

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
    EXPECT_TRUE(IsCanConfigMatchingTarget(config));
}

TEST(CanSocketSetupTests, DownStateRequiresReconfiguration) {
    auto output = MakeTargetCanFdOutput();
    const auto pos = output.find("state UP");
    ASSERT_NE(pos, std::string::npos);
    output.replace(pos, 8, "state DOWN");
    const auto config = ParseCanDetails(output);
    EXPECT_FALSE(config.up);
    EXPECT_FALSE(IsCanConfigMatchingTarget(config));
}

TEST(CanSocketSetupTests, MissingFdRequiresReconfiguration) {
    const auto config = ParseCanDetails(MakeClassicCanOutput());
    EXPECT_FALSE(IsCanConfigMatchingTarget(config));
}

TEST(CanSocketSetupTests, WrongBitrateRequiresReconfiguration) {
    auto output = MakeTargetCanFdOutput();
    const auto pos = output.find("bitrate 1000000");
    ASSERT_NE(pos, std::string::npos);
    output.replace(pos, 15, "bitrate 500000");
    const auto config = ParseCanDetails(output);
    EXPECT_EQ(config.bitrate, 500000);
    EXPECT_FALSE(IsCanConfigMatchingTarget(config));
}

TEST(CanSocketSetupTests, WrongSamplePointRequiresReconfiguration) {
    auto output = MakeTargetCanFdOutput();
    const auto pos = output.find("sample-point 0.765");
    ASSERT_NE(pos, std::string::npos);
    output.replace(pos, 18, "sample-point 0.900");
    const auto config = ParseCanDetails(output);
    EXPECT_NEAR(config.sample_point, 0.900, kTolerance);
    EXPECT_FALSE(IsCanConfigMatchingTarget(config));
}

TEST(CanSocketSetupTests, WrongDbitrateRequiresReconfiguration) {
    auto output = MakeTargetCanFdOutput();
    const auto pos = output.find("dbitrate 5000000");
    ASSERT_NE(pos, std::string::npos);
    output.replace(pos, 16, "dbitrate 4000000");
    const auto config = ParseCanDetails(output);
    EXPECT_EQ(config.dbitrate, 4000000);
    EXPECT_FALSE(IsCanConfigMatchingTarget(config));
}

TEST(CanSocketSetupTests, WrongDsamplePointRequiresReconfiguration) {
    auto output = MakeTargetCanFdOutput();
    const auto pos = output.find("dsample-point 0.882");
    ASSERT_NE(pos, std::string::npos);
    output.replace(pos, 19, "dsample-point 0.990");
    const auto config = ParseCanDetails(output);
    EXPECT_NEAR(config.dsample_point, 0.990, kTolerance);
    EXPECT_FALSE(IsCanConfigMatchingTarget(config));
}

TEST(CanSocketSetupTests, SetupCommandIncludesTargetTiming) {
    const auto args = BuildCanSetupCommandArgs("can0");
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
