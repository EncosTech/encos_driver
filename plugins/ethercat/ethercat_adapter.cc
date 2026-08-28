#include "ethercat_adapter.h"

#include <stdexcept>

#include "bus/bus.h"
#include "platform/delay.h"
#include "utils/thread_priority.h"
#ifndef ENCOS_STATIC_MODE
#include <filesystem>

#include "platform/os.h"
#endif

namespace encos {
#ifndef ENCOS_STATIC_MODE
namespace fs = std::filesystem;
#endif

namespace {

void RequireLoopPriority() {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    bool priority_set = false;
    std::thread priority_thread([&priority_set]() {
        priority_set = utils::SetCurrentThreadPriority(50);
    });
    priority_thread.join();
    if (!priority_set) {
        throw std::runtime_error("Failed to authorize EtherCAT loop thread priority");
    }
#endif
}

}  // namespace

std::unordered_map<int, Bus*> EthercatAdapter::GetBuses() {
    std::unordered_map<int, Bus*> buses;
    for (int slave_idx = 0; slave_idx < static_cast<int>(bus_sizes_.size()); ++slave_idx) {
        int bus_size = bus_sizes_[slave_idx];
        for (int bus_idx = 0; bus_idx < bus_size; ++bus_idx) {
            auto bus = GetBus(slave_idx, bus_idx);
            buses[bus->GetBusIndex()] = bus;
        }
    }
    return buses;
}

bool EthercatAdapter::Ok() {
    return running_.load();
}

void EthercatAdapter::Send(const MotorMessage& message) {
    if (ec_master_) {
        ec_master_->Send(message);
    }
}

void EthercatAdapter::Send(const MotorMessages& messages) {
    if (ec_master_) {
        ec_master_->Send(messages);
    }
}

void EthercatAdapter::SendSynchronized(const MotorMessages& messages) {
    if (ec_master_) {
        ec_master_->SendSynchronized(messages);
    }
}

EthercatAdapter::EthercatAdapter(const std::string& interface_name, const std::string& logger_name,
                                 encos::LogLevel log_level)
    : BaseAdapter(interface_name, logger_name, log_level) {
#ifdef ENCOS_STATIC_MODE
    ec_master_ = std::make_shared<EthercatHandle>(interface_name, Logger());
#else
    auto plugin_dir = platform::PluginDir();
#if defined(_WIN32)
    fs::path broker_path = plugin_dir / "EthercatFdBrokerExecutable.exe";
#else
    fs::path broker_path = plugin_dir / "EthercatFdBrokerExecutable";
#endif

    ec_master_ = std::make_shared<EthercatHandle>(interface_name, broker_path.string(), Logger());
#endif
    ec_master_->SetReceiveCallback([this](const MotorMessages& messages) {
        this->OnMessage(messages);
    });
    bus_sizes_ = ec_master_->GetBusSizes();
    RequireLoopPriority();
    running_.store(true);
    loop_thread_ = std::thread(&EthercatAdapter::Loop, this);
    platform::SleepFor(std::chrono::milliseconds(100));
}

void EthercatAdapter::Loop() {
    if (!utils::SetCurrentThreadPriority(50)) {
        Logger()->error("Failed to set EtherCAT loop thread priority after authorization");
        return;
    }
    if (ec_master_) {
        ec_master_->Loop();
    }
}

void EthercatAdapter::Stop() {
    if (!running_.exchange(false)) {
        return;
    }

    Logger()->info("Stopping EtherCAT adapter...");
    if (ec_master_) {
        ec_master_->RequestStop();
    }
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
    if (ec_master_) {
        ec_master_->Stop();
    }
}

EthercatAdapter::~EthercatAdapter() {
    Stop();
}

EthercatAdapter* EthercatAdapter::Create(const std::string& interface_name,
                                         const std::string& logger_name,
                                         encos::LogLevel log_level) {
    return new EthercatAdapter(interface_name, logger_name, log_level);
}

}  // namespace encos
