// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
#include "node/native_api.h"

#include <stdexcept>

#include "encos/encos_driver.h"
#include "node/native_devices.h"
#include "node/native_statistics.h"

namespace encos::node {
namespace {
int LogicalMotorIndex(const Json& value) {
    if (value.is_number() && value < 0) {
        return CheckedInteger<int>(value);
    }
    return LegacyCanIdBits(CheckedInteger<std::uint32_t>(value));
}

template <typename T>
T Require(wasm::Result<T> result) {
    if (!result.Ok())
        throw wasm::ErrorState{result.code, result.message};
    return result.value;
}
Json Dispose(wasm::ErrorCode code) {
    if (code != wasm::ErrorCode::Ok)
        throw std::runtime_error("native object disposal failed");
    return true;
}
Json MotorMap(wasm::RuntimeStore& store, std::uint32_t bus,
              const std::unordered_map<int, Motor*>& motors) {
    Json out = Json::array();
    for (const auto& [index, motor] : motors)
        out.push_back({{"index", index}, {"handle", Require(store.AdoptMotor(bus, motor))}});
    return out;
}
}  // namespace
Json InvokeNative(wasm::RuntimeStore& store, const std::string& op, const Json& a) {
    static const std::unordered_map<std::string, std::string> manager_aliases = {
        {"manager.createAdapter", "runtime.createAdapter"},
        {"manager.createBus", "adapter.getBus"},
        {"manager.createMotor", "bus.getMotor"},
        {"manager.createBattery", "bus.getBattery"},
        {"manager.createImu", "bus.getImu"},
        {"manager.createPms", "bus.getPms"},
        {"manager.createGlove", "adapter.getGlove"},
        {"manager.findMotor", "bus.selectMotor"},
        {"manager.getMotors", "bus.getMotors"},
        {"manager.getBuses", "adapter.getBuses"},
        {"manager.destroyAdapter", "adapter.dispose"},
        {"manager.destroyBus", "bus.dispose"},
        {"manager.destroyMotor", "motor.dispose"},
        {"manager.destroyBattery", "battery.dispose"},
        {"manager.destroyImu", "imu.dispose"},
        {"manager.destroyPms", "pms.dispose"},
        {"manager.destroyGlove", "glove.dispose"}};
    if (auto alias = manager_aliases.find(op); alias != manager_aliases.end())
        return InvokeNative(store, alias->second, a);
    if (op == "runtime.getVersion")
        return encos_get_version();
    if (op == "runtime.getAvailableAdapterTypes")
        return GetAvailableAdapterTypes();
    if (op == "runtime.getAvailableInterface")
        return GetAvailableInterface(a.at("type").get<std::string>());
    if (op == "runtime.clearLogger") {
        ClearLogger();
        return nullptr;
    }
    if (op == "runtime.createAdapter")
        return Require(store.CreateAdapter(
            a.at("type"), a.at("interfaceName"), a.value("loggerName", std::string{}),
            LogLevelFromInt(CheckedInteger<int>(a.value("logLevel", Json(2)))),
            a.value("extend", Json::object())));
    if (op == "manager.makeReceiveUniqueId")
        return EncosDriverManager::MakeReceiveUniqueId(
            CheckedInteger<int>(a.at("busIndex")), CheckedInteger<std::uint32_t>(a.at("canId")));
    auto h = a.contains("handle") ? CheckedInteger<std::uint32_t>(a.at("handle")) : 0;
    if (op == "motor.getIndex") {
        store.RefreshMotorIndices();
        return Require(store.ResolveMotor(h))->motor_idx;
    }
    if (op == "bus.dispose")
        return Dispose(store.DisposeBus(h));
    if (op == "motor.dispose")
        return Dispose(store.DisposeMotor(h));
    if (op == "battery.dispose")
        return Dispose(store.DisposeBattery(h));
    if (op == "imu.dispose")
        return Dispose(store.DisposeImu(h));
    if (op == "pms.dispose")
        return Dispose(store.DisposePms(h));
    if (op == "dexHand.dispose")
        return Dispose(store.DisposeDexHand(h));
    if (op == "glove.dispose")
        return Dispose(store.DisposeGlove(h));
    if (op == "adapter.dispose")
        return Dispose(store.DisposeAdapter(h));
    if (op == "manager.dispatchReceive" || op == "manager.dispatchUnknownReceive") {
        auto* adapter = Require(store.ResolveAdapter(h))->adapter;
        const auto& message = a.at("message");
        MotorPackMsg frame{};
        frame.id = CheckedInteger<std::uint32_t>(message.at("id"));
        frame.frame_flags = CheckedInteger<std::uint8_t>(message.value("frameFlags", Json(0)));
        const auto& bytes = message.at("data");
        if (!bytes.is_array() || bytes.size() > sizeof(frame.data))
            throw std::invalid_argument("frame data must contain at most 64 bytes");
        frame.len = CheckedInteger<std::uint8_t>(message.value("len", Json(bytes.size())));
        if (frame.len > sizeof(frame.data) || frame.len > bytes.size())
            throw std::invalid_argument("frame length exceeds supplied bytes");
        for (std::size_t i = 0; i < bytes.size(); ++i)
            frame.data[i] = CheckedInteger<std::uint8_t>(bytes[i]);
        int index = CheckedInteger<int>(a.at("busIndex"));
        return op == "manager.dispatchReceive"
                   ? EncosDriverManager::Instance().DispatchReceive(adapter, index, frame)
                   : EncosDriverManager::Instance().DispatchUnknownReceive(adapter, index, frame);
    }
    if (op == "manager.migrateMotorIndex") {
        auto motor = Require(store.ResolveMotor(h));
        bool result = EncosDriverManager::Instance().MigrateMotorIndex(
            motor->motor, LogicalMotorIndex(a.at("index")));
        store.RefreshMotorIndices();
        return result;
    }
    if (op.rfind("adapter.", 0) == 0) {
        auto entry = Require(store.ResolveAdapter(h));
        auto* adapter = entry->adapter;
        if (op == "adapter.getBus")
            return Require(store.GetBus(h, CheckedInteger<int>(a.value("slaveIndex", Json(-1))),
                                        CheckedInteger<int>(a.value("busIndex", Json(0)))));
        if (op == "adapter.getGlove")
            return Require(
                store.AdoptGlove(h, adapter->GetGlove(CheckedInteger<int>(a.at("slaveIndex"))),
                                 CheckedInteger<int>(a.at("slaveIndex"))));
        if (op == "adapter.getBuses") {
            Json out = Json::array();
            for (const auto& [index, bus] : adapter->GetBuses())
                out.push_back({{"index", index}, {"handle", Require(store.AdoptBus(h, bus))}});
            return out;
        }
        if (op == "adapter.hasFakeAdapterControl")
            return adapter->GetFakeAdapterControl() != nullptr;
        if (op == "adapter.ok")
            return adapter->Ok();

        if (op == "adapter.getStatistics")
            return StatisticsJson(adapter->GetStatistics());

        if (op == "adapter.getInterfaceName")
            return adapter->GetInterfaceName();
        if (op == "adapter.updateBusExtend")
            return adapter->UpdateBusExtend(a.at("extend").is_string()
                                                ? a.at("extend").get<std::string>()
                                                : a.at("extend").dump());
        if (op == "adapter.commit") {
            adapter->Commit();
            return nullptr;
        }
        if (op == "adapter.setSyncMode") {
            adapter->SetSyncMode(a.at("enabled"));
            return nullptr;
        }
        if (op == "adapter.getMotorStatus") {
            if (a.contains("motorIndex")) {
                auto status = adapter->GetMotorStatus(
                    CheckedInteger<int>(a.at("busIndex")), LogicalMotorIndex(a.at("motorIndex")),
                    CheckedInteger<int>(a.value("lifeCycleDeduction", Json(1))));
                return status ? MotorStatusJson(*status) : Json(nullptr);
            }
            Json out = Json::array();
            for (const auto& [id, status] : adapter->GetMotorStatus())
                out.push_back({{"id", id}, {"status", MotorStatusJson(status)}});
            return out;
        }
        if (op == "adapter.setMaxStatusLifeCycle") {
            adapter->SetMaxStatusLifeCycle(CheckedInteger<int>(a.at("maxLifeCycle")));
            return nullptr;
        }
        if (op == "adapter.getMaxStatusLifeCycle")
            return adapter->GetMaxStatusLifeCycle();
        if (op == "adapter.setStatusMedianFilterWindowSize") {
            adapter->SetStatusMedianFilterWindowSize(
                CheckedInteger<std::size_t>(a.at("windowSize")));
            return nullptr;
        }
        if (op == "adapter.setStatusLimitFilterMaxDeltas") {
            adapter->SetStatusLimitFilterMaxDeltas(a.at("position"), a.at("speed"), a.at("current"),
                                                   a.at("motorTemperature"),
                                                   a.at("mosTemperature"));
            return nullptr;
        }
    }
    if (op.rfind("bus.", 0) == 0) {
        auto* bus = Require(store.ResolveBus(h))->bus;
        if (op == "bus.getIndex")
            return bus->GetBusIndex();
        if (op == "bus.getMotor") {
            Motor* motor = nullptr;
            int index = LogicalMotorIndex(a.at("index"));
            auto& manager = EncosDriverManager::Instance();
            auto flags = CheckedInteger<std::uint8_t>(a.value("frameFlags", Json(0)));
            if (a.contains("model")) {
                auto model = static_cast<MotorModel>(CheckedInteger<int>(a.at("model")));
                motor = a.contains("canfd")
                            ? manager.CreateMotor(bus, index, model, flags, a.at("canfd"))
                            : manager.CreateMotor(bus, index, model, flags);
            } else if (a.contains("properties")) {
                auto properties = ParseMotorProperties(a.at("properties"));
                motor = a.contains("canfd")
                            ? manager.CreateMotor(bus, index, properties, flags, a.at("canfd"))
                            : manager.CreateMotor(bus, index, properties, flags);
            } else
                motor = a.contains("canfd") ? manager.CreateMotor(bus, index, flags, a.at("canfd"))
                                            : manager.CreateMotor(bus, index, flags);
            return Require(store.AdoptMotor(h, motor));
        }
        if (op == "bus.selectMotor") {
            auto* motor = bus->SelectMotor(LogicalMotorIndex(a.at("index")));
            return motor ? Json(Require(store.AdoptMotor(h, motor))) : Json(nullptr);
        }
        if (op == "bus.getMotors")
            return MotorMap(store, h, bus->GetMotors());
        if (op == "bus.scanMotors")
            return MotorMap(store, h, bus->ScanMotors());
        if (op == "bus.getBattery") {
            int index = CheckedInteger<int>(a.at("index"));
            return Require(store.AdoptBattery(h, bus->GetBattery(index), index));
        }
        if (op == "bus.getImu") {
            int index = CheckedInteger<int>(a.at("index"));
            return Require(store.AdoptImu(h, bus->GetImu(index), index));
        }
        if (op == "bus.getPms")
            return Require(store.AdoptPms(h, bus->GetPms()));
        if (op == "bus.getDexHand")
            return Require(
                store.AdoptDexHand(h, bus->GetDexHand(CheckedInteger<int>(a.at("baseId")))));
        if (op == "bus.startImu")
            return bus->StartImu();
        if (op == "bus.stopImu")
            return bus->StopImu();
        if (op == "bus.detectExternalDevice")
            return bus->DetectExternalDevice();
        if (op == "bus.hasExternalDevice")
            return bus->HasExternalDevice();
        if (op == "bus.resetMotorsId") {
            bool result = bus->ResetMotorsId(a.value("waitForAck", true));
            store.RefreshMotorIndices();
            return result;
        }
        if (op == "bus.startAutoAssignCanId")
            return bus->StartAutoAssignCanId(CheckedInteger<std::uint16_t>(a.at("startIndex")));
        if (op == "bus.stopAutoAssignCanId")
            return bus->StopAutoAssignCanId();
        if (op == "bus.commit") {
            bus->Commit();
            return nullptr;
        }
        if (op == "bus.setSyncMode") {
            bus->SetSyncMode(a.at("enabled"));
            return nullptr;
        }
    }
    if (op.rfind("motor.", 0) == 0) {
        auto result = InvokeMotor(store, op, a);
        if (op == "motor.setId")
            store.RefreshMotorIndices();
        return result;
    }
    if (op.rfind("battery.", 0) == 0 || op.rfind("imu.", 0) == 0 || op.rfind("pms.", 0) == 0 ||
        op.rfind("glove.", 0) == 0 || op.rfind("dexHand.", 0) == 0)
        return InvokeDevice(store, op, a);
    if (op.rfind("fake.", 0) == 0)
        return InvokeFake(store, op, a);
    return InvokeUtility(store, op, a);
}
std::function<void()> BindNativeCallback(wasm::RuntimeStore& store, const std::string& op,
                                         const Json& a, Emit emit) {
    if (op == "adapter.onStatus") {
        auto* adapter = Require(store.ResolveAdapter(a.at("handle")))->adapter;
        int bus = CheckedInteger<int>(a.at("busIndex")),
            motor = LogicalMotorIndex(a.at("motorIndex"));
        adapter->SetOnStatus(
            bus, motor,
            emit ? std::function<void(const MotorStatus&)>([emit](const MotorStatus& status) {
                emit(MotorStatusJson(status));
            })
                 : nullptr);
        return [adapter, bus, motor] {
            adapter->SetOnStatus(bus, motor, nullptr);
        };
    }
    if (op.rfind("fake.", 0) == 0)
        return BindFakeCallback(store, op, a, std::move(emit));
    return BindDeviceCallback(store, op, a, std::move(emit));
}
}  // namespace encos::node
