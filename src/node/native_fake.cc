// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
#include "node/native_devices.h"
#include "node/native_utilities.h"
#include "utils/thread_priority.h"

namespace encos::node {
namespace {
FakeAdapter* Fake(wasm::RuntimeStore& store, const Json& args) {
    auto result = store.ResolveFakeAdapter(CheckedInteger<uint32_t>(args.at("handle")));
    if (!result.Ok())
        throw wasm::ErrorState{result.code, result.message};
    return result.value;
}
Json Snapshot(const FakeMotorSnapshot& s) {
    Json j = {{"model", static_cast<int>(s.model)}, {"ranges", MotorPropertiesJson(s.ranges)}};
    j["positionRad"] = s.position_rad;
    j["speedRadS"] = s.speed_rad_s;
    j["currentA"] = s.current_a;
    j["torqueNm"] = s.torque_nm;
    j["motorTempC"] = s.motor_temp_c;
    j["mosTempC"] = s.mos_temp_c;
    j["acceleration"] = s.acceleration;
    j["kt"] = s.kt;
    j["canTimeoutMs"] = s.can_timeout_ms;
    j["replyFrameFlags"] = s.reply_frame_flags;
    j["communicationMode"] = static_cast<int>(s.communication_mode);
    j["error"] = static_cast<int>(s.error);
    j["brakeEnabled"] = s.brake_enabled;
    j["productId"] = s.product_id;
    j["hwVersion"] = s.hw_version;
    j["calFlag"] = s.cal_flag;
    j["rawAngleDeg"] = s.raw_angle_deg;
    j["calAngleDeg"] = s.cal_angle_deg;
    if (s.fw_version)
        j["fwVersion"] = *s.fw_version;
    j["curGainRaw"] = {{"kp", s.cur_gain_raw.kp}, {"ki", s.cur_gain_raw.ki}};
    j["spdGainRaw"] = {{"kp", s.spd_gain_raw.kp}, {"ki", s.spd_gain_raw.ki}};
    j["posGainRaw"] = {{"kp", s.pos_gain_raw.kp}, {"kd", s.pos_gain_raw.kd}};
    return j;
}
FakeMotorSnapshot ParseSnapshot(FakeAdapter& fake, const Json& j) {
    auto s = fake.CreateSnapshot(static_cast<MotorModel>(CheckedInteger<int>(j.at("model"))));
    if (j.contains("ranges"))
        s.ranges = ParseMotorProperties(j.at("ranges"));
    if (j.contains("positionRad"))
        s.position_rad = j.at("positionRad").get<decltype(s.position_rad)>();
    if (j.contains("speedRadS"))
        s.speed_rad_s = j.at("speedRadS").get<decltype(s.speed_rad_s)>();
    if (j.contains("currentA"))
        s.current_a = j.at("currentA").get<decltype(s.current_a)>();
    if (j.contains("torqueNm"))
        s.torque_nm = j.at("torqueNm").get<decltype(s.torque_nm)>();
    if (j.contains("motorTempC"))
        s.motor_temp_c = j.at("motorTempC").get<decltype(s.motor_temp_c)>();
    if (j.contains("mosTempC"))
        s.mos_temp_c = j.at("mosTempC").get<decltype(s.mos_temp_c)>();
    if (j.contains("acceleration"))
        s.acceleration = j.at("acceleration").get<decltype(s.acceleration)>();
    if (j.contains("kt"))
        s.kt = j.at("kt").get<decltype(s.kt)>();
    if (j.contains("canTimeoutMs"))
        s.can_timeout_ms = CheckedInteger<uint16_t>(j.at("canTimeoutMs"));
    if (j.contains("replyFrameFlags"))
        s.reply_frame_flags = CheckedInteger<uint8_t>(j.at("replyFrameFlags"));
    if (j.contains("communicationMode"))
        s.communication_mode =
            static_cast<MotorCommunicationMode>(CheckedInteger<uint8_t>(j.at("communicationMode")));
    if (j.contains("error"))
        s.error = static_cast<MotorError>(CheckedInteger<uint8_t>(j.at("error")));
    if (j.contains("brakeEnabled"))
        s.brake_enabled = j.at("brakeEnabled").get<decltype(s.brake_enabled)>();
    if (j.contains("productId"))
        s.product_id = CheckedInteger<uint32_t>(j.at("productId"));
    if (j.contains("hwVersion"))
        s.hw_version = CheckedInteger<uint32_t>(j.at("hwVersion"));
    if (j.contains("calFlag"))
        s.cal_flag = CheckedInteger<uint8_t>(j.at("calFlag"));
    if (j.contains("rawAngleDeg"))
        s.raw_angle_deg = j.at("rawAngleDeg").get<decltype(s.raw_angle_deg)>();
    if (j.contains("calAngleDeg"))
        s.cal_angle_deg = j.at("calAngleDeg").get<decltype(s.cal_angle_deg)>();
    s.fw_version = j.contains("fwVersion") && !j.at("fwVersion").is_null()
                       ? std::optional<uint32_t>(CheckedInteger<uint32_t>(j.at("fwVersion")))
                       : std::nullopt;
    if (j.contains("curGainRaw"))
        s.cur_gain_raw = {CheckedInteger<uint16_t>(j.at("curGainRaw").at("kp")),
                          CheckedInteger<uint16_t>(j.at("curGainRaw").at("ki"))};
    if (j.contains("spdGainRaw"))
        s.spd_gain_raw = {CheckedInteger<uint16_t>(j.at("spdGainRaw").at("kp")),
                          CheckedInteger<uint16_t>(j.at("spdGainRaw").at("ki"))};
    if (j.contains("posGainRaw"))
        s.pos_gain_raw = {CheckedInteger<uint16_t>(j.at("posGainRaw").at("kp")),
                          CheckedInteger<uint16_t>(j.at("posGainRaw").at("kd"))};
    return s;
}
Json Record(const FakeCommandRecord& r) {
    Json p = Json::object();
    std::visit(
        [&p](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, FakePVTControlPayload>) {
                p["kp"] = v.kp;
                p["kd"] = v.kd;
                p["position"] = v.position;
                p["speed"] = v.speed;
                p["torque"] = v.torque;
            }
            if constexpr (std::is_same_v<T, FakePosControlPayload>) {
                p["position"] = v.position;
                p["speed"] = v.speed;
                p["current"] = v.current;
                p["feedbackType"] = v.feedback_type;
            }
            if constexpr (std::is_same_v<T, FakeSpdControlPayload>) {
                p["speed"] = v.speed;
                p["current"] = v.current;
                p["feedbackType"] = v.feedback_type;
            }
            if constexpr (std::is_same_v<T, FakeCurControlPayload>) {
                p["current"] = v.current;
                p["feedbackType"] = v.feedback_type;
            }
            if constexpr (std::is_same_v<T, FakeTorControlPayload>) {
                p["torque"] = v.torque;
                p["feedbackType"] = v.feedback_type;
            }
            if constexpr (std::is_same_v<T, FakeStopPayload>) {
                p["mode"] = static_cast<int>(v.mode);
                p["current"] = v.current;
                p["feedbackType"] = v.feedback_type;
            }
            if constexpr (std::is_same_v<T, FakeBrakePayload>) {
                p["enabled"] = v.enabled;
                p["waitForAck"] = v.wait_for_ack;
            }
            if constexpr (std::is_same_v<T, FakeSetIdPayload>) {
                p["sourceId"] = v.source_id;
                p["targetId"] = v.target_id;
                p["waitForAck"] = v.wait_for_ack;
            }
            if constexpr (std::is_same_v<T, FakeSetPosPayload>) {
                p["positionRad"] = v.position_rad;
                p["waitForAck"] = v.wait_for_ack;
            }
            if constexpr (std::is_same_v<T, FakeResetZeroPosPayload>) {
                p["waitForAck"] = v.wait_for_ack;
                p["isLegacyReset"] = v.is_legacy_reset;
            }
            if constexpr (std::is_same_v<T, FakeGetParameterPayload>) {
                p["parameter"] = static_cast<int>(v.parameter);
            }
            if constexpr (std::is_same_v<T, FakeSetParameterPayload>) {
                p["rawParameterId"] = v.raw_parameter_id;
                p["rawValue"] = v.raw_value;
                p["waitForAck"] = v.wait_for_ack;
                if (v.parameter)
                    p["parameter"] = static_cast<int>(*v.parameter);
            }
        },
        r.payload);
    return {{"busIndex", r.bus_idx},
            {"motorIndex", r.motor_idx},
            {"rawFrameFlags", r.raw_frame_flags},
            {"kind", static_cast<int>(r.kind)},
            {"formattedText", r.formatted_text},
            {"payload", p}};
}
Json Message(const MotorMessage& m) {
    return {{"busIndex", m.bus_idx},
            {"data",
             {{"id", m.data.id},
              {"frameFlags", m.data.frame_flags},
              {"len", m.data.len},
              {"data", std::vector<uint8_t>(m.data.data, m.data.data + sizeof(m.data.data))}}}};
}
std::vector<uint8_t> Bytes(const Json& value) {
    if (!value.is_array())
        throw std::invalid_argument("payload must be an array");
    std::vector<uint8_t> bytes;
    for (const auto& item : value)
        bytes.push_back(CheckedInteger<uint8_t>(item));
    return bytes;
}
MotorMessage ParseMessage(const Json& j) {
    MotorMessage m{};
    m.bus_idx = CheckedInteger<int>(j.at("busIndex"));
    const auto& d = j.at("data");
    m.data.id = CheckedInteger<uint32_t>(d.at("id"));
    m.data.frame_flags = CheckedInteger<uint8_t>(d.at("frameFlags"));
    m.data.len = CheckedInteger<uint8_t>(d.at("len"));
    auto bytes = Bytes(d.at("data"));
    if (bytes.size() > sizeof(m.data.data) || m.data.len > sizeof(m.data.data) ||
        m.data.len > bytes.size())
        throw std::invalid_argument("invalid CAN payload length");
    std::copy(bytes.begin(), bytes.end(), m.data.data);
    return m;
}
}  // namespace
Json InvokeFake(wasm::RuntimeStore& store, const std::string& op, const Json& a) {
    auto* f = Fake(store, a);
    if (op == "fake.createSnapshot")
        return Snapshot(
            f->CreateSnapshot(static_cast<MotorModel>(CheckedInteger<int>(a.at("model")))));
    if (op == "fake.disableAutoCreateMotor") {
        f->DisableAutoCreateMotor();
        return nullptr;
    }
    if (op == "fake.enableCommandRecording") {
        f->EnableCommandRecording(a.value("enabled", true));
        return nullptr;
    }
    if (op == "fake.enablePositionError") {
        f->EnablePositionError(a.value("enabled", true));
        return nullptr;
    }
    if (op == "fake.clearCommandRecords") {
        f->ClearCommandRecords();
        return nullptr;
    }
    if (op == "fake.getFormattedSentCommands")
        return f->GetFormattedSentCommands();
    if (op == "fake.getDecodedCommandRecords") {
        Json j = Json::array();
        for (const auto& r : f->GetCommandRecords())
            j.push_back(Record(r));
        return j;
    }
    if (op == "fake.getReplyRecords") {
        Json j = Json::array();
        for (const auto& r : f->GetReplyRecords())
            j.push_back({{"busIndex", r.bus_idx},
                         {"motorIndex", r.motor_idx},
                         {"commandKind", static_cast<int>(r.command_kind)},
                         {"policy", static_cast<int>(r.policy)},
                         {"automatic", r.automatic}});
        return j;
    }
    if (op == "fake.getRawSentMessages") {
        Json j = Json::array();
        for (const auto& r : f->GetRawSentMessages())
            j.push_back(Message(r));
        return j;
    }
    if (op == "fake.injectMessage") {
        f->InjectMessage(ParseMessage(a.at("message")));
        return nullptr;
    }
    int bus = CheckedInteger<int>(a.at("busIndex")),
        motor = CheckedInteger<int>(a.at("motorIndex"));
    if (op == "fake.getMotorSnapshot")
        return Snapshot(f->GetMotorSnapshot(bus, motor));
    if (op == "fake.seedMotor") {
        if (a.contains("snapshot"))
            f->SeedMotor(bus, motor, ParseSnapshot(*f, a.at("snapshot")));
        else {
            FakeSeedOptions o;
            if (a.contains("options") && a.at("options").contains("replyFrameFlags"))
                o.reply_frame_flags =
                    CheckedInteger<uint8_t>(a.at("options").at("replyFrameFlags"));
            f->SeedMotor(bus, motor, static_cast<MotorModel>(CheckedInteger<int>(a.at("model"))),
                         o);
        }
        return nullptr;
    }
    if (op == "fake.makeFeedbackMessage") {
        const auto& j = a.at("status");
        MotorStatus s;
        s.error = static_cast<MotorError>(CheckedInteger<uint8_t>(j.at("error")));
        s.position = j.at("position");
        s.speed = j.at("speed");
        s.current = j.at("current");
        s.motor_temperature = j.at("motorTemperature");
        s.mos_temperature = j.at("mosTemperature");
        return Message(f->MakeFeedbackMessage(
            bus, motor, s, CheckedInteger<int>(a.value("feedbackType", Json(1)))));
    }
    if (op == "fake.makeParameterReply" || op == "fake.makeWriteAck") {
        auto parameter = static_cast<MotorParameter>(CheckedInteger<uint8_t>(a.at("parameter")));
        auto bytes = Bytes(a.at("payload"));
        if (bytes.size() > (op == "fake.makeWriteAck" ? 5U : 6U))
            throw std::invalid_argument("parameter payload exceeds CAN frame capacity");
        auto flags = CheckedInteger<uint8_t>(a.value("frameFlags", Json(0)));
        return Message(op == "fake.makeParameterReply"
                           ? f->MakeParameterReply(bus, motor, parameter, bytes, flags)
                           : f->MakeWriteAck(bus, motor, parameter, bytes, flags));
    }
    throw std::invalid_argument("unknown Fake operation: " + op);
}
std::function<void()> BindFakeCallback(wasm::RuntimeStore& store, const std::string& op,
                                       const Json& args, Emit emit) {
    if (op != "fake.decodedCommand")
        throw std::invalid_argument("unknown Fake callback");
    auto* f = Fake(store, args);
    f->SetDecodedCommandObserver([emit = std::move(emit)](const FakeCommandRecord& record) {
        emit(Record(record));
    });
    return [f] {
        f->ClearDecodedCommandObserver();
    };
}
Json InvokeUtility(wasm::RuntimeStore& store, const std::string& op, const Json& a) {
    if (op.rfind("utility.", 0) == 0)
        return InvokeLogUtility(store, op, a);
    if (op == "runtime.getMotorModelRanges")
        return MotorPropertiesJson(
            GetMotorModelRanges(static_cast<MotorModel>(CheckedInteger<int>(a.at("model")))));
    if (op == "runtime.stringToMotorModel")
        return static_cast<int>(StringToMotorModel(a.at("name").get<std::string>()));
    if (op == "runtime.motorModelToString")
        return MotorModelToString(static_cast<MotorModel>(CheckedInteger<int>(a.at("model"))));
    if (op == "runtime.getAllMotorModelStrings")
        return GetAllMotorModelStrings();
    if (op == "runtime.setCurrentThreadPriority")
        return utils::SetCurrentThreadPriority(CheckedInteger<int>(a.at("priority")));
    throw std::invalid_argument("unknown runtime operation: " + op);
}
}  // namespace encos::node
