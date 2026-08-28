#include "plugins/fake/fake_protocol.h"

#include <cmath>
#include <cstring>
#include <sstream>

#include "motor/math_utils.h"

namespace encos {

namespace {

float DecodeFloat(const uint8_t* data) {
    uint32_t raw = (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
                   (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

float DecodeSpeedCommand(const MotorPackMsg& pack) {
    return DecodeFloat(pack.data + 1) * static_cast<float>(M_PI) / 30.0f;
}

}  // namespace

std::optional<MotorParameter> FakeWriteParameterFromRawId(uint8_t raw_id) {
    switch (raw_id) {
        case 0x01:
            return MotorParameter::Acceleration;
        case 0x04:
            return MotorParameter::Kt;
        case 0x05:
            return MotorParameter::PVTKpRange;
        case 0x06:
            return MotorParameter::PVTKdRange;
        case 0x07:
            return MotorParameter::PVTPosRange;
        case 0x08:
            return MotorParameter::PVTSpdRange;
        case 0x09:
            return MotorParameter::PVTTorRange;
        case 0x0A:
            return MotorParameter::PVTCurRange;
        case 0x0B:
            return MotorParameter::CanTimeout;
        case 0x0C:
            return MotorParameter::CurKpKi;
        case 0x0D:
            return MotorParameter::SpdKpKi;
        case 0x0E:
            return MotorParameter::PosKpKd;
        default:
            return std::nullopt;
    }
}

uint8_t FakeRawIdFromWriteParameter(MotorParameter parameter) {
    switch (parameter) {
        case MotorParameter::Acceleration:
            return 0x01;
        case MotorParameter::Kt:
            return 0x04;
        case MotorParameter::PVTKpRange:
            return 0x05;
        case MotorParameter::PVTKdRange:
            return 0x06;
        case MotorParameter::PVTPosRange:
            return 0x07;
        case MotorParameter::PVTSpdRange:
            return 0x08;
        case MotorParameter::PVTTorRange:
            return 0x09;
        case MotorParameter::PVTCurRange:
            return 0x0A;
        case MotorParameter::CanTimeout:
            return 0x0B;
        case MotorParameter::CurKpKi:
            return 0x0C;
        case MotorParameter::SpdKpKi:
            return 0x0D;
        case MotorParameter::PosKpKd:
            return 0x0E;
        default:
            return static_cast<uint8_t>(parameter);
    }
}

std::optional<FakeCommandRecord> DecodeFakeCommand(const MotorMessage& message,
                                                   const FakeMotorSnapshot* snapshot) {
    FakeCommandRecord record;
    record.bus_idx = message.bus_idx;
    record.motor_idx = static_cast<int>(message.data.id);
    record.raw_frame_flags = message.data.frame_flags;

    const auto& pack = message.data;

    // 0x7FF 是扩展 ID 广播地址，实际目标电机 ID 在 data[0..1] 中。
    if (pack.id == 0x7FF && pack.len >= 2) {
        record.motor_idx = (static_cast<int>(pack.data[0]) << 8) | pack.data[1];
    }

    if (pack.id == 0x7FF && pack.len == 6 && pack.data[3] == 0x04) {
        FakeSetIdPayload payload;
        payload.source_id = (static_cast<int>(pack.data[0]) << 8) | pack.data[1];
        payload.target_id = (static_cast<int>(pack.data[4]) << 8) | pack.data[5];
        payload.wait_for_ack = true;
        record.kind = FakeCommandKind::SetId;
        record.payload = payload;
        std::ostringstream ss;
        ss << "SetId(from=" << payload.source_id << ", to=" << payload.target_id << ")";
        record.formatted_text = ss.str();
        return record;
    }

    if (pack.id == 0x7FF && pack.len == 6 && pack.data[3] == 0x03) {
        FakeSetPosPayload payload;
        const int16_t centideg = static_cast<int16_t>((pack.data[4] << 8) | pack.data[5]);
        payload.position_rad =
            static_cast<float>(centideg) / 100.0f * static_cast<float>(M_PI) / 180.0f;
        record.kind = FakeCommandKind::SetPos;
        record.payload = payload;
        std::ostringstream ss;
        ss << "SetPos(position=" << payload.position_rad << ")";
        record.formatted_text = ss.str();
        return record;
    }

    if (pack.id == 0x7FF && pack.len == 4 && pack.data[3] == 0x03) {
        FakeResetZeroPosPayload payload;
        payload.is_legacy_reset = true;
        record.kind = FakeCommandKind::ResetZeroPos;
        record.payload = payload;
        record.formatted_text = "ResetZeroPos(legacy=true)";
        return record;
    }

    if (pack.len == 2 && pack.data[0] == static_cast<uint8_t>(0x07 << 5)) {
        FakeGetParameterPayload payload;
        payload.parameter = static_cast<MotorParameter>(pack.data[1]);
        record.kind = FakeCommandKind::GetParameter;
        record.payload = payload;
        std::ostringstream ss;
        ss << "GetParameter(param=" << static_cast<int>(payload.parameter) << ")";
        record.formatted_text = ss.str();
        return record;
    }

    if (pack.len == 3 && pack.data[0] == static_cast<uint8_t>(0x04 << 5)) {
        FakeBrakePayload payload;
        payload.enabled = pack.data[1] != 0;
        payload.wait_for_ack = true;
        record.kind = FakeCommandKind::Brake;
        record.payload = payload;
        record.formatted_text = payload.enabled ? "Brake(enable=true)" : "Brake(enable=false)";
        return record;
    }

    if (pack.len == 7 && (pack.data[0] & 0xE0) == 0x40) {
        FakeSpdControlPayload payload;
        payload.feedback_type = pack.data[0] & 0x1F;
        payload.speed = DecodeSpeedCommand(pack);
        payload.current =
            static_cast<float>((static_cast<uint16_t>(pack.data[5]) << 8) | pack.data[6]) / 10.0f;
        record.kind = FakeCommandKind::SpdControl;
        record.payload = payload;

        std::ostringstream ss;
        ss << "SpdControl(speed=" << payload.speed << ", current=" << payload.current
           << ", feedback=" << payload.feedback_type << ")";
        record.formatted_text = ss.str();
        return record;
    }

    if (pack.len == 8 && (pack.data[0] & 0xE0) == 0x20) {
        FakePosControlPayload payload;
        uint8_t pos_bytes[4] = {
            static_cast<uint8_t>(((pack.data[3] & 0x1F) << 3) | (pack.data[4] >> 5)),
            static_cast<uint8_t>(((pack.data[2] & 0x1F) << 3) | (pack.data[3] >> 5)),
            static_cast<uint8_t>(((pack.data[1] & 0x1F) << 3) | (pack.data[2] >> 5)),
            static_cast<uint8_t>(((pack.data[0] & 0x1F) << 3) | (pack.data[1] >> 5)),
        };
        float position_deg = 0.0f;
        std::memcpy(&position_deg, pos_bytes, sizeof(position_deg));
        const int spd_int = ((pack.data[4] & 0x1F) << 10) | (static_cast<int>(pack.data[5]) << 2) |
                            (static_cast<int>(pack.data[6]) >> 6);
        const int cur_int = ((pack.data[6] & 0x3F) << 6) | (pack.data[7] >> 2);
        payload.position = position_deg * static_cast<float>(M_PI) / 180.0f;
        payload.speed = UintToFloat(spd_int, 0.0f, 3276.7f, 15) * static_cast<float>(M_PI) / 30.0f;
        payload.current = UintToFloat(cur_int, 0.0f, 409.5f, 12);
        payload.feedback_type = pack.data[7] & 0x03;
        record.kind = FakeCommandKind::PosControl;
        record.payload = payload;
        std::ostringstream ss;
        ss << "PosControl(position=" << payload.position << ", speed=" << payload.speed
           << ", current=" << payload.current << ", feedback=" << payload.feedback_type << ")";
        record.formatted_text = ss.str();
        return record;
    }

    if (pack.len == 3 && (pack.data[0] & 0xE0) == 0x60) {
        const int selector = (pack.data[0] >> 2) & 0x07;
        if (selector == 0) {
            FakeCurControlPayload payload;
            const int16_t raw = static_cast<int16_t>((pack.data[1] << 8) | pack.data[2]);
            payload.current = static_cast<float>(raw) / 100.0f;
            payload.feedback_type = pack.data[0] & 0x03;
            record.kind = FakeCommandKind::CurControl;
            record.payload = payload;
            std::ostringstream ss;
            ss << "CurControl(current=" << payload.current << ", feedback=" << payload.feedback_type
               << ")";
            record.formatted_text = ss.str();
            return record;
        }
        if (selector == 1) {
            FakeTorControlPayload payload;
            const int16_t raw = static_cast<int16_t>((pack.data[1] << 8) | pack.data[2]);
            payload.torque = static_cast<float>(raw) / 100.0f;
            payload.feedback_type = pack.data[0] & 0x03;
            record.kind = FakeCommandKind::TorControl;
            record.payload = payload;
            std::ostringstream ss;
            ss << "TorControl(torque=" << payload.torque << ", feedback=" << payload.feedback_type
               << ")";
            record.formatted_text = ss.str();
            return record;
        }

        FakeStopPayload payload;
        const int16_t raw = static_cast<int16_t>((pack.data[1] << 8) | pack.data[2]);
        payload.mode = static_cast<MotorStopMode>(selector);
        payload.current = static_cast<float>(raw) / 100.0f;
        payload.feedback_type = pack.data[0] & 0x03;
        record.kind = FakeCommandKind::Stop;
        record.payload = payload;
        std::ostringstream ss;
        ss << "Stop(mode=" << selector << ", current=" << payload.current
           << ", feedback=" << payload.feedback_type << ")";
        record.formatted_text = ss.str();
        return record;
    }

    if (pack.len == 8) {
        FakePVTControlPayload payload;
        const int kp_int = (pack.data[0] << 7) | (pack.data[1] >> 1);
        const int kd_int = ((pack.data[1] & 0x01) << 8) | pack.data[2];
        const int pos_int = (pack.data[3] << 8) | pack.data[4];
        const int spd_int = (pack.data[5] << 4) | (pack.data[6] >> 4);
        const int tor_int = ((pack.data[6] & 0x0F) << 8) | pack.data[7];
        const auto kp_min = snapshot != nullptr ? snapshot->ranges.kp.min : 0.0f;
        const auto kp_max = snapshot != nullptr ? snapshot->ranges.kp.max : 500.0f;
        const auto kd_min = snapshot != nullptr ? snapshot->ranges.kd.min : 0.0f;
        const auto kd_max = snapshot != nullptr ? snapshot->ranges.kd.max : 5.0f;
        const auto pos_min = snapshot != nullptr ? snapshot->ranges.position.min : -12.5f;
        const auto pos_max = snapshot != nullptr ? snapshot->ranges.position.max : 12.5f;
        const auto spd_min = snapshot != nullptr ? snapshot->ranges.speed.min : -18.0f;
        const auto spd_max = snapshot != nullptr ? snapshot->ranges.speed.max : 18.0f;
        const auto tor_min = snapshot != nullptr ? snapshot->ranges.torque.min : -30.0f;
        const auto tor_max = snapshot != nullptr ? snapshot->ranges.torque.max : 30.0f;

        payload.kp = UintToFloat(kp_int, kp_min, kp_max, 12);
        payload.kd = UintToFloat(kd_int, kd_min, kd_max, 9);
        payload.position = UintToFloat(pos_int, pos_min, pos_max, 16);
        payload.speed = UintToFloat(spd_int, spd_min, spd_max, 12);
        payload.torque = UintToFloat(tor_int, tor_min, tor_max, 12);
        record.kind = FakeCommandKind::PVTControl;
        record.payload = payload;
        std::ostringstream ss;
        ss << "PVTControl(kp=" << payload.kp << ", kd=" << payload.kd
           << ", position=" << payload.position << ", speed=" << payload.speed
           << ", torque=" << payload.torque << ")";
        record.formatted_text = ss.str();
        return record;
    }

    if (pack.len >= 3 && (pack.data[0] & 0xE0) == 0xC0) {
        FakeSetParameterPayload payload;
        payload.raw_parameter_id = pack.data[1];
        payload.parameter = FakeWriteParameterFromRawId(pack.data[1]);
        payload.wait_for_ack = (pack.data[0] & 0x01) != 0;
        payload.raw_value.assign(pack.data + 2, pack.data + pack.len);
        record.kind = FakeCommandKind::SetParameter;
        record.payload = payload;

        std::ostringstream ss;
        ss << "SetParameter(raw=" << static_cast<int>(payload.raw_parameter_id)
           << ", size=" << payload.raw_value.size() << ")";
        record.formatted_text = ss.str();
        return record;
    }

    record.formatted_text = "Unknown";
    return record;
}

}  // namespace encos
