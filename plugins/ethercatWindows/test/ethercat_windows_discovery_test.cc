#include <gtest/gtest.h>

#include "bus/bus.h"
#include "managed_adapter_test.h"
#include "motor/motor.h"
#include "plugins/fake/fake_adapter.h"

namespace encos {

namespace {

constexpr int kMotorScanCandidateCount = 0x7FF;
constexpr int kSeededMotorCount = 3;

constexpr int ExpectedMotorScanPositionQueries() {
    return kMotorScanCandidateCount + kSeededMotorCount;
}

}  // namespace

TEST(EthercatWindowsDiscoveryTests, SeededMotorsDriveDiscoveryFlags) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-discovery");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2, FakeSeedOptions{0});
    adapter->SeedMotor(0, 2, MotorModel::EC_A4310_P2, FakeSeedOptions{kCanFrameFlagEff});
    adapter->SeedMotor(
        0, 3, MotorModel::EC_A4310_P2,
        FakeSeedOptions{static_cast<uint8_t>(kCanFrameFlagEff | kCanFrameFlagFdMask)});

    auto bus = adapter->GetBus(0, 0);
    const auto discovered = bus->ScanMotors();

    ASSERT_EQ(discovered.size(), 3);
    EXPECT_FALSE(discovered.at(1)->IsCanEffEnabled());
    EXPECT_FALSE(discovered.at(1)->IsCanFdEnabled());
    EXPECT_TRUE(discovered.at(2)->IsCanEffEnabled());
    EXPECT_FALSE(discovered.at(2)->IsCanFdEnabled());
    EXPECT_TRUE(discovered.at(3)->IsCanEffEnabled());
    EXPECT_TRUE(discovered.at(3)->IsCanFdEnabled());

    int position_queries = 0;
    int range_queries = 0;
    for (const auto& command : adapter->GetCommandRecords()) {
        if (command.kind != FakeCommandKind::GetParameter) {
            continue;
        }
        const auto* payload = std::get_if<FakeGetParameterPayload>(&command.payload);
        if (payload == nullptr) {
            continue;
        }
        if (payload->parameter == MotorParameter::Position) {
            EXPECT_EQ(CanFrameFlagsUseCanFd(command.raw_frame_flags),
                      position_queries >= kMotorScanCandidateCount);
            if (position_queries >= kMotorScanCandidateCount) {
                EXPECT_GE(command.motor_idx, 1);
                EXPECT_LE(command.motor_idx, kSeededMotorCount);
            }
            position_queries++;
            continue;
        }
        EXPECT_EQ(CanFrameFlagsUseCanFd(command.raw_frame_flags), command.motor_idx == 3);
        EXPECT_EQ(CanFrameFlagsUseExtendedId(command.raw_frame_flags), command.motor_idx != 1);
        range_queries++;
    }
    EXPECT_EQ(position_queries, ExpectedMotorScanPositionQueries());
    EXPECT_EQ(range_queries, 21);
}

}  // namespace encos
