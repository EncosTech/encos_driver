#include <array>
#include <exception>
#include <iostream>
#include <string>

#include "adapter/fake_adapter_control.h"
#include "encos/encos_driver.h"

namespace {

bool IsUnsupported(const std::exception& error) {
    return std::string(error.what()).find("File logging is unsupported on Emscripten") !=
           std::string::npos;
}

}  // namespace

int main() {
    bool writer_rejected = false;
    try {
        encos::LogWriter<1> writer("unsupported", std::array<std::string, 1>{"value"});
    } catch (const std::exception& error) {
        writer_rejected = IsUnsupported(error);
    }
    if (!writer_rejected) {
        std::cerr << "LogWriter did not report the Emscripten logging boundary\n";
        return 1;
    }

    auto adapter = encos::MakeAdapter("Fake", "wasm-logging-test");
    auto control = adapter ? adapter->GetFakeAdapterControl() : nullptr;
    if (!control) {
        std::cerr << "Fake adapter control is unavailable\n";
        return 1;
    }
    control->EnableAutoCreateMotor();
    auto motor = adapter->GetBus(0)->GetMotor(1, encos::MotorModel::EC_A4310_P2);

    bool motor_rejected = false;
    try {
        motor->EnableLog("unsupported");
    } catch (const std::exception& error) {
        motor_rejected = IsUnsupported(error);
    }
    if (!motor_rejected) {
        std::cerr << "Motor::EnableLog did not report the Emscripten logging boundary\n";
        return 1;
    }
    return 0;
}
