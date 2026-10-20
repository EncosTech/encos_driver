// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>

#include "bus/bus.h"
#include "device/motor/motor.h"
#include "device/motor/types.h"
#include "plugins/adapter/fake/fake_adapter.h"
#include "test_fixtures.h"

namespace encos {

namespace {

/**
 * @brief 构造一帧反馈类型 1 的原始报文
 *
 * 直接给出原始字节而非经由假适配器编码，才能精确断言温度与故障码的解码公式。
 * @param raw_error D[0] 低 5 位的故障码
 * @param raw_motor_temp D[6] 电机温度原始值
 * @param raw_mos_temp D[7] FET 温度原始值
 * @return 打包好的反馈报文
 */
MotorPackMsg MakeRawType1Feedback(uint8_t raw_error, uint8_t raw_motor_temp, uint8_t raw_mos_temp) {
    MotorPackMsg pack{};
    pack.id = 1;
    pack.len = 8;
    pack.data[0] = static_cast<uint8_t>((1U << 5) | raw_error);
    pack.data[1] = 0x80;
    pack.data[2] = 0x00;
    pack.data[3] = 0x80;
    pack.data[4] = 0x00;
    pack.data[5] = 0x00;
    pack.data[6] = raw_motor_temp;
    pack.data[7] = raw_mos_temp;
    return pack;
}

/**
 * @brief 取最近一次发送的原始报文
 *
 * `GetRawSentMessages()` 按值返回，因此必须先持有容器再取元素，不能直接绑定引用。
 */
MotorPackMsg LastSentPacket(FakeAdapter& adapter) {
    const auto messages = adapter.GetRawSentMessages();
    if (messages.empty()) {
        ADD_FAILURE() << "no message was sent";
        return MotorPackMsg{};
    }
    return messages.back().data;
}

}  // namespace

/** @brief 旧版固件（夹具默认）的版本相关行为 */
class LegacyFirmwareTests : public MotorTestFixture {};

TEST_F(LegacyFirmwareTests, ConstructionProbeRecordsVersionZero) {
    EXPECT_EQ(motor->GetProperties().fw_version, 0U);
}

TEST_F(LegacyFirmwareTests, ConstructionProbeIsTheOnlyMessageForModelInitialization) {
    adapter->SeedMotor(0, 2, MotorModel::EC_A4310_P2);
    adapter->ClearCommandRecords();

    ASSERT_NE(bus->GetMotor(2, MotorModel::EC_A4310_P2), nullptr);

    // 型号初始化只发一帧固件版本查询，不应额外查询 PVT 范围。
    const auto messages = adapter->GetRawSentMessages();
    ASSERT_EQ(messages.size(), 1U);
    const auto& pack = messages.front().data;
    EXPECT_EQ(pack.id, 2U);
    EXPECT_EQ(pack.len, 2);
    EXPECT_EQ(pack.data[0], 0xE0);
    EXPECT_EQ(pack.data[1], static_cast<uint8_t>(MotorParameter::FirmwareVersion));
}

TEST_F(LegacyFirmwareTests, DecodesTemperatureWithLegacyFormula) {
    adapter->InjectMessage(MotorMessage{0, MakeRawType1Feedback(0, 61, 61)});

    const auto status = motor->GetStatus();
    ASSERT_TRUE(status.has_value());
    // 旧版：T = (raw - 50) / 2
    EXPECT_FLOAT_EQ(status->motor_temperature, 5.5F);
    EXPECT_FLOAT_EQ(status->mos_temperature, 5.5F);
}

TEST_F(LegacyFirmwareTests, DecodesLegacyErrorCodes) {
    const std::pair<uint8_t, MotorError> cases[] = {
        {6, MotorError::BrakeVoltageHigh},
        {7, MotorError::DriverError},
        {8, MotorError::OverTemperatureWarning},
    };
    for (const auto& [raw, expected] : cases) {
        adapter->InjectMessage(MotorMessage{0, MakeRawType1Feedback(raw, 61, 61)});
        const auto status = motor->GetStatus();
        ASSERT_TRUE(status.has_value());
        EXPECT_EQ(status->error, expected) << "raw=" << static_cast<int>(raw);
    }
}

TEST_F(LegacyFirmwareTests, StopSendsRequestedControlMode) {
    motor->Stop<0>(MotorStopMode::RegenerativeBrake, 3.0F);

    const auto pack = LastSentPacket(*adapter);
    // 0x60 | (CTRL_MODE 4 << 2) | ACK 0
    EXPECT_EQ(pack.data[0], 0x70);
    EXPECT_EQ(pack.data[1], 0x01);
    EXPECT_EQ(pack.data[2], 0x2C);
}

TEST_F(LegacyFirmwareTests, BrakeUsesElectromagneticBrakeControl) {
    EXPECT_TRUE(motor->Brake(true));

    const auto pack = LastSentPacket(*adapter);
    EXPECT_EQ(pack.data[0], 0x75);
    EXPECT_EQ(pack.data[2], 0x00);
}

TEST_F(LegacyFirmwareTests, SetCommunicationModeSendsModeWithAckBit) {
    EXPECT_TRUE(motor->SetCommunicationMode(MotorCommunicationMode::CanFd, true));

    const auto pack = LastSentPacket(*adapter);
    EXPECT_EQ(pack.data[0], 0xC1);
    EXPECT_EQ(pack.data[1], 0x02);
    EXPECT_EQ(pack.data[2], static_cast<uint8_t>(MotorCommunicationMode::CanFd));
}

TEST_F(LegacyFirmwareTests, SetSpdPiUsesLegacyGainScale) {
    EXPECT_TRUE(motor->SetSpdPI(0.1F, 0.2F, true));

    const auto pack = LastSentPacket(*adapter);
    EXPECT_EQ(pack.data[0], 0xC0);
    EXPECT_EQ(pack.data[1], 0x0D);
    // 0.1 × 100000 = 10000
    EXPECT_EQ(pack.data[2], 0x27);
    EXPECT_EQ(pack.data[3], 0x10);
    // 0.2 × 100000 = 20000
    EXPECT_EQ(pack.data[4], 0x4E);
    EXPECT_EQ(pack.data[5], 0x20);
}

TEST_F(LegacyFirmwareTests, SpdPiRoundTripsThroughLegacyScale) {
    ASSERT_TRUE(motor->SetSpdPI(0.1F, 0.2F, true));

    const auto gains = motor->GetParameter<MotorParameter::SpdKpKi>();
    EXPECT_NEAR(gains.kp, 0.1F, 1e-4F);
    EXPECT_NEAR(gains.ki, 0.2F, 1e-4F);
}

TEST_F(LegacyFirmwareTests, PosPdRoundTripsThroughLegacyScale) {
    ASSERT_TRUE(motor->SetPosPD(0.3F, 0.05F, true));

    const auto gains = motor->GetParameter<MotorParameter::PosKpKd>();
    EXPECT_NEAR(gains.kp, 0.3F, 1e-4F);
    EXPECT_NEAR(gains.kd, 0.05F, 1e-4F);
}

TEST_F(LegacyFirmwareTests, HasBrakeMatchesProperties) {
    EXPECT_EQ(motor->HasBrake(), motor->GetProperties().brake);
}

TEST_F(LegacyFirmwareTests, PropertiesSetterDoesNotOverwriteCapabilityOrVersion) {
    auto properties = motor->GetProperties();
    properties.brake = true;
    properties.fw_version = 0x12345678U;
    properties.kt = 42.0F;
    properties.position = {-1.0F, 1.0F};
    motor->SetProperties(properties);

    const auto after = motor->GetProperties();
    EXPECT_FALSE(after.brake);
    EXPECT_EQ(after.fw_version, 0U);
    EXPECT_NE(after.kt, 42.0F);
    // 范围字段照常被写入
    EXPECT_FLOAT_EQ(after.position.min, -1.0F);
    EXPECT_FLOAT_EQ(after.position.max, 1.0F);
}

TEST_F(LegacyFirmwareTests, DeprecatedAliasesStillForward) {
    MotorProperties properties = motor->GetProperties();
    properties.speed = {-2.0F, 2.0F};
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    motor->SetDriverPVTRanges(properties);
    const auto deprecated = motor->GetPVTRanges();
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    EXPECT_FLOAT_EQ(deprecated.speed.min, -2.0F);
    EXPECT_FLOAT_EQ(deprecated.speed.max, 2.0F);
}

/** @brief 新版固件的版本相关行为 */
class NewFirmwareTests : public NewFirmwareMotorTestFixture {};

TEST_F(NewFirmwareTests, ConstructionProbeRecordsFirmwareVersion) {
    EXPECT_EQ(motor->GetProperties().fw_version, NewFirmwareMotorTestFixture::kFirmwareVersion);
}

TEST_F(NewFirmwareTests, ReadsProductHardwareAndFirmwareVersion) {
    EXPECT_EQ(motor->GetParameter<MotorParameter::ProductId>(),
              NewFirmwareMotorTestFixture::kProductId);
    EXPECT_EQ(motor->GetParameter<MotorParameter::HardwareVersion>(),
              NewFirmwareMotorTestFixture::kHardwareVersion);
    EXPECT_EQ(motor->GetParameter<MotorParameter::FirmwareVersion>(),
              NewFirmwareMotorTestFixture::kFirmwareVersion);
}

TEST_F(NewFirmwareTests, ReadsEncoderAngle) {
    const auto angle = motor->GetParameter<MotorParameter::EncoderAngle>();
    EXPECT_EQ(angle.cal_flag, 1U);
    EXPECT_FLOAT_EQ(angle.raw_angle, 30.0F);
    EXPECT_FLOAT_EQ(angle.cal_angle, 29.5F);
}

TEST_F(NewFirmwareTests, DecodesTemperatureWithNewFormula) {
    adapter->InjectMessage(MotorMessage{0, MakeRawType1Feedback(0, 61, 61)});

    const auto status = motor->GetStatus();
    ASSERT_TRUE(status.has_value());
    // 新版：T = raw - 55
    EXPECT_FLOAT_EQ(status->motor_temperature, 6.0F);
    EXPECT_FLOAT_EQ(status->mos_temperature, 6.0F);
}

TEST_F(NewFirmwareTests, DecodesNewErrorCodes) {
    const std::pair<uint8_t, MotorError> cases[] = {
        {6, MotorError::OutputEncoderError},  {7, MotorError::FetOverTemperature},
        {8, MotorError::OverSpeed},           {9, MotorError::DriverError},
        {31, MotorError::TemperatureWarning},
    };
    for (const auto& [raw, expected] : cases) {
        adapter->InjectMessage(MotorMessage{0, MakeRawType1Feedback(raw, 61, 61)});
        const auto status = motor->GetStatus();
        ASSERT_TRUE(status.has_value());
        EXPECT_EQ(status->error, expected) << "raw=" << static_cast<int>(raw);
    }
}

TEST_F(NewFirmwareTests, SetCommunicationModeIsUnsupportedAndSendsNothing) {
    EXPECT_FALSE(motor->SetCommunicationMode(MotorCommunicationMode::CanFd, true));
    EXPECT_FALSE(motor->SetCommunicationMode(MotorCommunicationMode::ClassicCan, false));
    EXPECT_TRUE(adapter->GetRawSentMessages().empty());
}

TEST_F(NewFirmwareTests, BrakeIsUnsupportedAndSendsNothing) {
    EXPECT_FALSE(motor->Brake(true));
    EXPECT_FALSE(motor->Brake(false));
    EXPECT_TRUE(adapter->GetRawSentMessages().empty());
}

TEST_F(NewFirmwareTests, StopMapsEveryModeToDampingBrake) {
    for (const auto mode : {MotorStopMode::FullBrake, MotorStopMode::DynamicBrake,
                            MotorStopMode::RegenerativeBrake}) {
        adapter->ClearCommandRecords();
        motor->Stop<0>(mode, 3.0F);

        const auto pack = LastSentPacket(*adapter);
        // 统一 CTRL_MODE 2；current 作为制动电流上限仍照常编码。
        EXPECT_EQ(pack.data[0], 0x68);
        EXPECT_EQ(pack.data[1], 0x01);
        EXPECT_EQ(pack.data[2], 0x2C);
    }
}

TEST_F(NewFirmwareTests, SetSpdPiUsesNewGainScale) {
    EXPECT_TRUE(motor->SetSpdPI(0.1F, 0.2F, true));

    const auto pack = LastSentPacket(*adapter);
    EXPECT_EQ(pack.data[0], 0xC0);
    EXPECT_EQ(pack.data[1], 0x0D);
    // 0.1 × 500 = 50
    EXPECT_EQ(pack.data[2], 0x00);
    EXPECT_EQ(pack.data[3], 0x32);
    // 0.2 × 500 = 100
    EXPECT_EQ(pack.data[4], 0x00);
    EXPECT_EQ(pack.data[5], 0x64);
}

TEST_F(NewFirmwareTests, SpdPiRoundTripsThroughNewScale) {
    ASSERT_TRUE(motor->SetSpdPI(0.1F, 0.2F, true));

    const auto gains = motor->GetParameter<MotorParameter::SpdKpKi>();
    EXPECT_NEAR(gains.kp, 0.1F, 1e-4F);
    EXPECT_NEAR(gains.ki, 0.2F, 1e-4F);
}

TEST_F(NewFirmwareTests, PosPdRoundTripsThroughNewScale) {
    ASSERT_TRUE(motor->SetPosPD(0.3F, 0.05F, true));

    const auto gains = motor->GetParameter<MotorParameter::PosKpKd>();
    EXPECT_NEAR(gains.kp, 0.3F, 1e-4F);
    EXPECT_NEAR(gains.kd, 0.05F, 1e-4F);
}

/** @brief 固件完全不认识 0x32（真机旧版固件）时的行为 */
class SilentFirmwareTests : public MotorTestFixture {
protected:
    FakeMotorSnapshot ConfigureSnapshot(FakeMotorSnapshot snapshot) override {
        snapshot.fw_version = std::nullopt;
        return snapshot;
    }

    std::string GetAdapterName() const override {
        return "SilentFirmwareMotorTestAdapter";
    }
};

TEST_F(SilentFirmwareTests, ProbeTimeoutRecordsVersionZero) {
    EXPECT_EQ(motor->GetProperties().fw_version, 0U);
}

TEST_F(SilentFirmwareTests, VersionQueryTimesOutWithException) {
    EXPECT_THROW((void) motor->GetParameter<MotorParameter::FirmwareVersion>(), std::runtime_error);
}

TEST_F(SilentFirmwareTests, OldSemanticsStillApply) {
    adapter->InjectMessage(MotorMessage{0, MakeRawType1Feedback(7, 61, 61)});

    const auto status = motor->GetStatus();
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->error, MotorError::DriverError);
    EXPECT_FLOAT_EQ(status->motor_temperature, 5.5F);
}

/** @brief 自动分配 CAN ID 的广播帧（0x08） */
class AutoAssignCanIdTests : public MotorTestFixture {};

TEST_F(AutoAssignCanIdTests, StartSendsAssignBroadcast) {
    EXPECT_TRUE(bus->StartAutoAssignCanId(2));

    const auto pack = LastSentPacket(*adapter);
    EXPECT_EQ(pack.id, 0x7FFU);
    EXPECT_EQ(pack.len, 4);
    EXPECT_EQ(pack.data[0], 0x00);
    EXPECT_EQ(pack.data[1], 0x02);
    EXPECT_EQ(pack.data[2], 0x00);
    EXPECT_EQ(pack.data[3], 0x08);
}

TEST_F(AutoAssignCanIdTests, StopSendsEndMarker) {
    EXPECT_TRUE(bus->StopAutoAssignCanId());

    const auto pack = LastSentPacket(*adapter);
    EXPECT_EQ(pack.id, 0x7FFU);
    EXPECT_EQ(pack.len, 4);
    EXPECT_EQ(pack.data[0], 0x07);
    EXPECT_EQ(pack.data[1], 0xFF);
    EXPECT_EQ(pack.data[3], 0x08);
}

TEST_F(AutoAssignCanIdTests, RejectsReservedAndZeroIds) {
    EXPECT_THROW(bus->StartAutoAssignCanId(0x000), std::invalid_argument);
    EXPECT_THROW(bus->StartAutoAssignCanId(0x7FF), std::invalid_argument);
    EXPECT_THROW(bus->StartAutoAssignCanId(0x800), std::invalid_argument);
}

}  // namespace encos
