#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <zstd.h>

#include "adapter/base_adapter.h"
#include "bus/bus.h"
#include "encos/driver_manager.h"
#include "motor/motor.h"

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
    encos::log_base_name = "/tmp/encos-motor-log-shutdown-" + std::to_string(getpid());
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
    return 0;
}
