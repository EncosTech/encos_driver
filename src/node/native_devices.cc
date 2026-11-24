// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
#include "node/native_devices.h"

#include "device/dex_hand/dex_hand.h"

namespace encos::node {
namespace {
template <typename T>
T Resolve(wasm::Result<T> result) {
    if (!result.Ok())
        throw wasm::ErrorState{result.code, result.message};
    return result.value;
}
template <typename T>
Json RangeJson(const Range<T>& value) {
    return {{"min", value.min}, {"max", value.max}};
}
template <typename T>
Range<T> ParseRange(const Json& value) {
    if constexpr (std::is_integral_v<T>) {
        return {CheckedInteger<T>(value.at("min")), CheckedInteger<T>(value.at("max"))};
    } else {
        return {value.at("min").get<T>(), value.at("max").get<T>()};
    }
}
Json ParameterJson(float value) {
    return value;
}
Json ParameterJson(int value) {
    return value;
}
Json ParameterJson(uint16_t value) {
    return value;
}
Json ParameterJson(uint32_t value) {
    return value;
}
template <typename T>
Json ParameterJson(const Range<T>& value) {
    return RangeJson(value);
}
Json ParameterJson(const KpKi<float>& value) {
    return {{"kp", value.kp}, {"ki", value.ki}};
}
Json ParameterJson(const KpKd<float>& value) {
    return {{"kp", value.kp}, {"kd", value.kd}};
}
Json ParameterJson(const MotorEncoderAngle& value) {
    return {
        {"calFlag", value.cal_flag}, {"rawAngle", value.raw_angle}, {"calAngle", value.cal_angle}};
}
Json GetParameter(Motor& motor, int parameter) {
    switch (parameter) {
        case static_cast<int>(MotorParameter::Position):
            return ParameterJson(motor.GetParameter<MotorParameter::Position>());
        case static_cast<int>(MotorParameter::Speed):
            return ParameterJson(motor.GetParameter<MotorParameter::Speed>());
        case static_cast<int>(MotorParameter::Current):
            return ParameterJson(motor.GetParameter<MotorParameter::Current>());
        case static_cast<int>(MotorParameter::Power):
            return ParameterJson(motor.GetParameter<MotorParameter::Power>());
        case static_cast<int>(MotorParameter::Kt):
            return ParameterJson(motor.GetParameter<MotorParameter::Kt>());
        case static_cast<int>(MotorParameter::PVTKpRange):
            return ParameterJson(motor.GetParameter<MotorParameter::PVTKpRange>());
        case static_cast<int>(MotorParameter::PVTKdRange):
            return ParameterJson(motor.GetParameter<MotorParameter::PVTKdRange>());
        case static_cast<int>(MotorParameter::PVTPosRange):
            return ParameterJson(motor.GetParameter<MotorParameter::PVTPosRange>());
        case static_cast<int>(MotorParameter::PVTSpdRange):
            return ParameterJson(motor.GetParameter<MotorParameter::PVTSpdRange>());
        case static_cast<int>(MotorParameter::PVTTorRange):
            return ParameterJson(motor.GetParameter<MotorParameter::PVTTorRange>());
        case static_cast<int>(MotorParameter::PVTCurRange):
            return ParameterJson(motor.GetParameter<MotorParameter::PVTCurRange>());
        case static_cast<int>(MotorParameter::UUID):
            return ParameterJson(motor.GetParameter<MotorParameter::UUID>());
        case static_cast<int>(MotorParameter::CanTimeout):
            return ParameterJson(motor.GetParameter<MotorParameter::CanTimeout>());
        case static_cast<int>(MotorParameter::CurKpKi):
            return ParameterJson(motor.GetParameter<MotorParameter::CurKpKi>());
        case static_cast<int>(MotorParameter::SpdKpKi):
            return ParameterJson(motor.GetParameter<MotorParameter::SpdKpKi>());
        case static_cast<int>(MotorParameter::PosKpKd):
            return ParameterJson(motor.GetParameter<MotorParameter::PosKpKd>());
        case static_cast<int>(MotorParameter::BrakeStatus):
            return ParameterJson(motor.GetParameter<MotorParameter::BrakeStatus>());
        case static_cast<int>(MotorParameter::EncoderAngle):
            return ParameterJson(motor.GetParameter<MotorParameter::EncoderAngle>());
        case static_cast<int>(MotorParameter::ProductId):
            return ParameterJson(motor.GetParameter<MotorParameter::ProductId>());
        case static_cast<int>(MotorParameter::HardwareVersion):
            return ParameterJson(motor.GetParameter<MotorParameter::HardwareVersion>());
        case static_cast<int>(MotorParameter::FirmwareVersion):
            return ParameterJson(motor.GetParameter<MotorParameter::FirmwareVersion>());
    }
    throw std::invalid_argument("unsupported motor parameter");
}
MotorStatus Normalize(const MotorStatus& value) {
    return value;
}
MotorStatus Normalize(const MotorFeedbackMsg2& value) {
    MotorStatus result;
    result.error = value.error;
    result.position = value.position;
    result.current = value.current;
    result.motor_temperature = value.motor_temperature;
    return result;
}
MotorStatus Normalize(const MotorFeedbackMsg3& value) {
    MotorStatus result;
    result.error = value.error;
    result.speed = value.speed;
    result.current = value.current;
    result.motor_temperature = value.motor_temperature;
    return result;
}
template <int Type, typename Function>
Json Control(Function&& function) {
    Json result = {
        {"ok", true}, {"feedbackType", Type}, {"hasFeedback", Type != 0}, {"noResponse", false}};
    if constexpr (Type == 0) {
        function();
    } else {
        const auto status = Normalize(function());
        result["noResponse"] = status.error == MotorError::NoResponse;
        result["feedback"] = MotorStatusJson(status);
    }
    return result;
}
template <int Type>
Json RunControl(Motor& motor, const std::string& operation, const Json& args, int feedback) {
    if (operation == "motor.posControl")
        return Control<Type>([&] {
            return motor.PosControl<Type>(args.at("position"), args.at("speed"), args.at("current"),
                                          feedback);
        });
    if (operation == "motor.spdControl")
        return Control<Type>([&] {
            return motor.SpdControl<Type>(args.at("speed"), args.at("current"), feedback);
        });
    if (operation == "motor.curControl")
        return Control<Type>([&] {
            return motor.CurControl<Type>(args.at("current"), feedback);
        });
    if (operation == "motor.torControl")
        return Control<Type>([&] {
            return motor.TorControl<Type>(args.at("torque"), feedback);
        });
    if (operation == "motor.stop") {
        const int mode = CheckedInteger<int>(args.at("mode"));
        if (mode < 2 || mode > 4)
            throw std::invalid_argument("invalid stop mode");
        return Control<Type>([&] {
            return motor.Stop<Type>(static_cast<MotorStopMode>(mode), args.at("current"), feedback);
        });
    }
    throw std::invalid_argument("unknown motor control operation");
}
Json BatteryStatusJson(const BatteryStatus& value) {
    const auto& e = value.error;
    Json result = {{"error",
                    {{"couldNotCharge", e.could_not_charge},
                     {"couldNotDischarge", e.could_not_discharge},
                     {"lowBattery", e.low_battery},
                     {"overCurrentSteady", e.over_current_steady},
                     {"overCurrentPeak", e.over_current_peak},
                     {"overCurrentCharge", e.over_current_charge},
                     {"batteryOverTemp", e.battery_over_temp},
                     {"mosOverTemp", e.mos_over_temp},
                     {"couldNotCommunicate", e.could_not_communicate},
                     {"stoppedEmergency", e.stopped_emergency},
                     {"chargerFault", e.charger_fault},
                     {"commTimeout", e.comm_timeout}}}};
    if (value.state) {
        const auto& s = *value.state;
        result["state"] = {{"isMaster", s.is_master},
                           {"soc", s.soc},
                           {"voltage", s.voltage},
                           {"allowedDischargeCurrent", s.allowed_discharge_current},
                           {"allowedChargeCurrent", s.allowed_charge_current}};
    }
    if (value.temp) {
        const auto& s = *value.temp;
        result["temp"] = {{"battery", s.battery},
                          {"mos", s.mos},
                          {"dischargeCurrent", s.discharge_current},
                          {"chargeCurrent", s.charge_current}};
    }
    if (value.active_commands) {
        const auto& s = *value.active_commands;
        result["activeCommands"] = {{"shutdownRequest", s.shutdown_request},
                                    {"dischargeRequest", s.discharge_request},
                                    {"forceShutdownBroadcast", s.force_shutdown_broadcast},
                                    {"allowCharging", s.allow_charging},
                                    {"faultShutdownBroadcast", s.fault_shutdown_broadcast},
                                    {"mosStatus", s.mos_status}};
    }
    return result;
}
Json ImuStatusJson(const ImuStatus& value) {
    Json result = Json::object();
    if (value.acceleration) {
        const auto& s = *value.acceleration;
        result["acceleration"] = {{"x", s.x}, {"y", s.y}, {"z", s.z}};
    }
    if (value.angular_velocity) {
        const auto& s = *value.angular_velocity;
        result["angularVelocity"] = {{"x", s.x}, {"y", s.y}, {"z", s.z}};
    }
    if (value.euler_angle) {
        const auto& s = *value.euler_angle;
        result["eulerAngle"] = {{"pitch", s.pitch}, {"roll", s.roll}, {"heading", s.heading}};
    }
    if (value.quaternion) {
        const auto& s = *value.quaternion;
        result["quaternion"] = {{"qw", s.qw}, {"qx", s.qx}, {"qy", s.qy}, {"qz", s.qz}};
    }
    return result;
}
Json PmsStatusJson(const PmsStatus& s) {
    return {{"v48ChannelEnabled", s.v48_channel_enabled},
            {"batterySoc", s.battery_soc},
            {"batteryVoltage", s.battery_voltage},
            {"batteryCurrent", s.battery_current},
            {"v5Current", s.v5_current},
            {"v48Currents", s.v48_currents},
            {"v19Currents", s.v19_currents}};
}
Json GloveStatusJson(const GloveStatus& status) {
    Json result = Json::array();
    for (const auto& finger : status) {
        Json row = Json::array();
        for (const auto& angle : finger)
            row.push_back(angle ? Json(*angle) : Json(nullptr));
        result.push_back(std::move(row));
    }
    return result;
}
Json DexHandValue(uint8_t value) {
    return value;
}
Json DexHandValue(const DexHandSamplePoints& value) {
    return {{"nominal", value.nominal}, {"data", value.data}};
}
template <typename T>
Json DexHandReplyJson(const DexHandReply<T>& reply) {
    Json result = {{"status", static_cast<int>(reply.status)}};
    if (reply.value) {
        result["value"] = DexHandValue(*reply.value);
    }
    return result;
}
Json InvokeDexHand(DexHand& hand, const std::string& op, const Json& args) {
    DexHandRequestOptions options;
    options.canfd = args.value("canfd", false);
    if (args.contains("timeoutMs")) {
        options.timeout = std::chrono::milliseconds(CheckedInteger<uint32_t>(args.at("timeoutMs")));
    }
    if (op == "dexHand.getBaseId") {
        return DexHandReplyJson(hand.GetBaseId(options));
    }
    if (op == "dexHand.setBaseId") {
        return DexHandReplyJson(hand.SetBaseId(CheckedInteger<int>(args.at("baseId")), options));
    }
    if (op == "dexHand.getSamplePoints") {
        return DexHandReplyJson(hand.GetSamplePoints(options));
    }
    if (op == "dexHand.setSamplePoints") {
        const auto& points = args.at("points");
        return DexHandReplyJson(
            hand.SetSamplePoints({CheckedInteger<uint16_t>(points.at("nominal")),
                                  CheckedInteger<uint16_t>(points.at("data"))},
                                 options));
    }
    throw std::invalid_argument("unknown DexHand operation");
}
}  // namespace
Json MotorStatusJson(const MotorStatus& status) {
    return {{"error", static_cast<int>(status.error)},
            {"position", status.position},
            {"speed", status.speed},
            {"current", status.current},
            {"motorTemperature", status.motor_temperature},
            {"mosTemperature", status.mos_temperature}};
}
Json MotorPropertiesJson(const MotorProperties& p) {
    return {{"kp", RangeJson(p.kp)},
            {"kd", RangeJson(p.kd)},
            {"position", RangeJson(p.position)},
            {"speed", RangeJson(p.speed)},
            {"torque", RangeJson(p.torque)},
            {"current", RangeJson(p.current)},
            {"kt", p.kt},
            {"brake", p.brake},
            {"fwVersion", p.fw_version}};
}
MotorProperties ParseMotorProperties(const Json& p) {
    return {ParseRange<float>(p.at("kp")),
            ParseRange<float>(p.at("kd")),
            ParseRange<float>(p.at("position")),
            ParseRange<float>(p.at("speed")),
            ParseRange<float>(p.at("torque")),
            ParseRange<float>(p.at("current")),
            p.at("kt").get<float>(),
            p.value("brake", false),
            CheckedInteger<uint32_t>(p.value("fwVersion", Json(0)))};
}
Json InvokeMotor(wasm::RuntimeStore& store, const std::string& op, const Json& a) {
    auto& m = *Resolve(store.ResolveMotor(CheckedInteger<uint32_t>(a.at("handle"))))->motor;
    const bool ack = a.value("waitForAck", true);
    if (op == "motor.getProperties" || op == "motor.getPvtRanges")
        return MotorPropertiesJson(m.GetProperties());
    if (op == "motor.hasBrake")
        return m.HasBrake();
    if (op == "motor.isLogged")
        return m.IsLogged();
    if (op == "motor.isCanFdEnabled")
        return m.IsCanFdEnabled();
    if (op == "motor.isCanEffEnabled")
        return m.IsCanEffEnabled();
    if (op == "motor.enableLog")
        m.EnableLog(a.at("baseName"));
    else if (op == "motor.disableLog")
        m.DisableLog();
    else if (op == "motor.initMotorPvtParam")
        m.InitMotorPVTParam();
    else if (op == "motor.setProperties" || op == "motor.setDriverPvtRanges")
        m.SetProperties(ParseMotorProperties(a.at("properties")));
    else if (op == "motor.setCurrentRange") {
        auto p = m.GetProperties();
        const float range = a.at("range");
        p.current = {-range, range};
        m.SetProperties(p);
    } else if (op == "motor.enableCanFd")
        m.EnableCanFd();
    else if (op == "motor.disableCanFd")
        m.DisableCanFd();
    else if (op == "motor.enableCanEff")
        m.EnableCanEff();
    else if (op == "motor.disableCanEff")
        m.DisableCanEff();
    else if (op == "motor.setId") {
        const bool result = m.SetId(CheckedInteger<uint16_t>(a.at("newIndex")), ack);
        store.RefreshMotorIndices();
        return result;
    } else if (op == "motor.setPosition" || op == "motor.setPos")
        return m.SetPos(a.at("position"));
    else if (op == "motor.resetZeroPosition" || op == "motor.resetZeroPos")
        return m.ResetZeroPos(ack);
    else if (op == "motor.getStatus") {
        const auto s = m.GetStatus(CheckedInteger<int>(a.value("lifeCycleDeduction", Json(1))));
        Json r = {{"ok", true}, {"hasValue", s.has_value()}};
        if (s)
            r["status"] = MotorStatusJson(*s);
        return r;
    } else if (op == "motor.setMaxStatusLifeCycle")
        m.SetMaxStatusLifeCycle(CheckedInteger<int>(a.at("maxLifeCycle")));
    else if (op == "motor.setStatusMedianFilterWindowSize")
        m.SetStatusMedianFilterWindowSize(CheckedInteger<size_t>(a.at("windowSize")));
    else if (op == "motor.setStatusLimitFilterMaxDeltas") {
        const auto& d = a.at("deltas");
        const float n = std::numeric_limits<float>::quiet_NaN();
        m.SetStatusLimitFilterMaxDeltas(d.value("position", n), d.value("speed", n),
                                        d.value("current", n), d.value("motorTemperature", n),
                                        d.value("mosTemperature", n));
    } else if (op == "motor.brake")
        return m.Brake(a.at("enabled"), ack);
    else if (op == "motor.setPvtKpRange")
        return m.SetPVTKpRange(ParseRange<uint16_t>(a.at("range")), ack);
    else if (op == "motor.setPvtKdRange")
        return m.SetPVTKdRange(ParseRange<uint16_t>(a.at("range")), ack);
    else if (op == "motor.setPvtPosRange")
        return m.SetPVTPosRange(ParseRange<float>(a.at("range")), ack);
    else if (op == "motor.setPvtSpdRange")
        return m.SetPVTSpdRange(ParseRange<float>(a.at("range")), ack);
    else if (op == "motor.setPvtTorRange")
        return m.SetPVTTorRange(ParseRange<float>(a.at("range")), ack);
    else if (op == "motor.setPvtCurRange")
        return m.SetPVTCurRange(ParseRange<float>(a.at("range")), ack);
    else if (op == "motor.setCurPi")
        return m.SetCurPI(a.at("kp"), a.at("ki"), ack);
    else if (op == "motor.setSpdPi")
        return m.SetSpdPI(a.at("kp"), a.at("ki"), ack);
    else if (op == "motor.setPosPd")
        return m.SetPosPD(a.at("kp"), a.at("kd"), ack);
    else if (op == "motor.setCanTimeout")
        return m.SetCanTimeout(CheckedInteger<uint16_t>(a.at("timeoutMs")), ack);
    else if (op == "motor.setCommunicationMode") {
        const int mode = CheckedInteger<int>(a.at("mode"));
        if (mode < 0 || mode > 2)
            throw std::invalid_argument("invalid communication mode");
        return m.SetCommunicationMode(static_cast<MotorCommunicationMode>(mode), ack);
    } else if (op == "motor.getParameter")
        return GetParameter(m, CheckedInteger<int>(a.at("parameter")));
    else {
        const int type = CheckedInteger<int>(a.value("feedbackType", Json(0)));
        const int feedback = CheckedInteger<int>(a.value("feedback", Json(type)));
        if (type < 0 || type > 3 || feedback < 0 || feedback > 3)
            throw std::invalid_argument("invalid feedback type");
        if (op == "motor.pvtControl") {
            if (type == 0)
                return Control<0>([&] {
                    m.PVTControl<0>(a.at("kp"), a.at("kd"), a.at("position"), a.at("speed"),
                                    a.at("torque"));
                });
            if (type == 1)
                return Control<1>([&] {
                    return m.PVTControl<1>(a.at("kp"), a.at("kd"), a.at("position"), a.at("speed"),
                                           a.at("torque"));
                });
            throw std::invalid_argument("PVT supports feedback type 0 or 1");
        }
        switch (type) {
            case 0:
                return RunControl<0>(m, op, a, feedback);
            case 1:
                return RunControl<1>(m, op, a, feedback);
            case 2:
                return RunControl<2>(m, op, a, feedback);
            default:
                return RunControl<3>(m, op, a, feedback);
        }
    }
    return nullptr;
}
Json InvokeDevice(wasm::RuntimeStore& store, const std::string& op, const Json& a) {
    if (op == "imu.nativeUpdateInterval")
        return Imu::NativeUpdateInterval().count();
    if (op == "imu.jsUpdateInterval")
        return Imu::JsUpdateInterval().count();
    const auto handle = CheckedInteger<uint32_t>(a.at("handle"));
    if (op.rfind("dexHand.", 0) == 0)
        return InvokeDexHand(*Resolve(store.ResolveDexHand(handle)), op, a);
    if (op.rfind("battery.", 0) == 0) {
        auto& b = *Resolve(store.ResolveBattery(handle))->battery;
        if (op == "battery.getStatus")
            return BatteryStatusJson(b.GetStatus());
        if (op == "battery.clearFault")
            b.ClearFault();
        else if (op == "battery.requestCharging")
            b.RequestCharging(a.at("enabled"));
        else if (op == "battery.allowDischarge")
            b.AllowDischarge(a.at("enabled"));
        else if (op == "battery.sendPassiveCommands") {
            const auto& c = a.at("commands");
            BatteryPassiveCommands commands;
            commands.allow_shutdown = c.value("allowShutdown", false);
            commands.allow_discharge = c.value("allowDischarge", false);
            commands.parallel_discharge = c.value("parallelDischarge", false);
            commands.force_shutdown = c.value("forceShutdown", false);
            commands.request_charging = c.value("requestCharging", false);
            commands.fault_shutdown_broadcast = c.value("faultShutdownBroadcast", false);
            commands.configure_fault_thresholds = c.value("configureFaultThresholds", false);
            commands.clear_fault = c.value("clearFault", false);
            commands.factory_mode = c.value("factoryMode", false);
            commands.debug = c.value("debug", false);
            b.SendPassiveCommands(commands);
        } else
            throw std::invalid_argument("unknown battery operation");
        return nullptr;
    }
    if (op == "imu.getStatus")
        return ImuStatusJson(Resolve(store.ResolveImu(handle))->imu->GetStatus());
    if (op.rfind("pms.", 0) == 0) {
        auto& p = *Resolve(store.ResolvePms(handle))->pms;
        if (op == "pms.getStatus") {
            const auto s = p.GetStatus();
            return s ? PmsStatusJson(*s) : Json(nullptr);
        }
        if (op == "pms.sendCommand") {
            p.SendCommand(static_cast<PmsCommand>(CheckedInteger<uint16_t>(a.at("command"))));
            return nullptr;
        }
    }
    if (op.rfind("glove.", 0) == 0) {
        auto& g = *Resolve(store.ResolveGlove(handle))->glove;
        if (op == "glove.getStatus")
            return GloveStatusJson(g.GetStatus());
        if (op == "glove.calibrateAll")
            return static_cast<int>(g.CalibrateAll());
        if (op == "glove.calibrateByMask")
            return static_cast<int>(
                g.CalibrateByMask(CheckedInteger<uint8_t>(a.at("fingerIndex")),
                                  CheckedInteger<uint16_t>(a.at("encoderMask"))));
    }
    throw std::invalid_argument("unknown device operation");
}
std::function<void()> BindDeviceCallback(wasm::RuntimeStore& store, const std::string& op,
                                         const Json& a, Emit emit) {
    const auto handle = CheckedInteger<uint32_t>(a.at("handle"));
    if (op == "motor.onStatus") {
        auto* p = Resolve(store.ResolveMotor(handle))->motor;
        p->SetOnStatus([emit = std::move(emit)](const MotorStatus& s) {
            emit(MotorStatusJson(s));
        });
        return [p] {
            p->SetOnStatus(nullptr);
        };
    }
    if (op == "battery.onStatus") {
        auto* p = Resolve(store.ResolveBattery(handle))->battery;
        p->SetOnStatus([emit = std::move(emit)](const BatteryStatus& s) {
            emit(BatteryStatusJson(s));
        });
        return [p] {
            p->SetOnStatus(nullptr);
        };
    }
    if (op == "imu.onStatus") {
        auto* p = Resolve(store.ResolveImu(handle))->imu;
        p->SetOnStatus([emit = std::move(emit)](const ImuStatus& s) {
            emit(ImuStatusJson(s));
        });
        return [p] {
            p->SetOnStatus(nullptr);
        };
    }
    if (op == "pms.onStatus") {
        auto* p = Resolve(store.ResolvePms(handle))->pms;
        p->SetOnStatus([emit = std::move(emit)](const PmsStatus& s) {
            emit(PmsStatusJson(s));
        });
        return [p] {
            p->SetOnStatus(nullptr);
        };
    }
    if (op == "glove.onStatus") {
        auto* p = Resolve(store.ResolveGlove(handle))->glove;
        p->SetOnStatus([emit = std::move(emit)](const GloveStatus& s) {
            emit(GloveStatusJson(s));
        });
        return [p] {
            p->SetOnStatus(nullptr);
        };
    }
    throw std::invalid_argument("unknown device callback");
}
}  // namespace encos::node
