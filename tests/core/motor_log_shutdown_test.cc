// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>
#include <zstd.h>

#include "adapter/base_adapter.h"
#include "bus/bus.h"
#include "device/motor/motor.h"
#include "encos/driver_manager.h"

namespace encos {
namespace {

std::string log_base_name;

std::string DecompressLog(const std::string& file_name) {
    std::ifstream input(file_name, std::ios::binary);
    const std::vector<char> compressed{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};
    ZSTD_DStream* stream = ZSTD_createDStream();
    if (stream == nullptr || ZSTD_isError(ZSTD_initDStream(stream))) {
        ZSTD_freeDStream(stream);
        return {};
    }

    std::string output;
    std::array<char, 4096> chunk{};
    ZSTD_inBuffer input_buffer{compressed.data(), compressed.size(), 0};
    while (input_buffer.pos < input_buffer.size) {
        ZSTD_outBuffer output_buffer{chunk.data(), chunk.size(), 0};
        const auto result = ZSTD_decompressStream(stream, &output_buffer, &input_buffer);
        if (ZSTD_isError(result)) {
            output.clear();
            break;
        }
        output.append(chunk.data(), output_buffer.pos);
    }
    ZSTD_freeDStream(stream);
    return output;
}

void VerifyLogsFlushed() {
    const std::string command_file = log_base_name + "_command.csv.zstd";
    const std::string status_file = log_base_name + "_status.csv.zstd";
    const auto command_log = DecompressLog(command_file);
    const auto status_log = DecompressLog(status_file);
    std::remove(command_file.c_str());
    std::remove(status_file.c_str());

    if (command_log.find("SpdControl") == std::string::npos ||
        status_log.find("timestamp_ns,error,position,speed,current,motor_temperature,") != 0) {
        std::_Exit(2);
    }
}

class ShutdownLogAdapter final : public BaseAdapter {
public:
    explicit ShutdownLogAdapter(const std::string& interface_name)
        : BaseAdapter(interface_name, "ShutdownLogAdapter", LogLevel::Off) {}

    std::unordered_map<int, Bus*> GetBuses() override {
        return GetKnownBusesSnapshot();
    }

    bool Ok() override {
        return true;
    }

protected:
    void Send(const MotorMessage&) override {}
};

}  // namespace
}  // namespace encos

int main() {
    using encos::EncosDriverManager;
    const auto unique_suffix =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    encos::log_base_name =
        (std::filesystem::temp_directory_path() / ("encos-motor-log-shutdown-" + unique_suffix))
            .string();
    if (std::atexit(encos::VerifyLogsFlushed) != 0) {
        return 3;
    }

    auto& manager = EncosDriverManager::Instance();
    const std::string interface_name = "motor-log-shutdown-test";
    auto* adapter = manager.CreateAdapterWithFactory(interface_name, [&interface_name] {
        return new encos::ShutdownLogAdapter(interface_name);
    });
    auto* motor = adapter->GetBus(0)->GetMotor(1, encos::MotorModel::EC_A4310_P2);
    motor->EnableLog(encos::log_base_name);
    motor->SpdControl<0>(1.0F, 2.0F);
    // Windows 上 DLL 静态析构晚于 exe 的 atexit，显式销毁以在进程退出前完成日志落盘。
    manager.DestroyAdapter(adapter);
    return 0;
}
