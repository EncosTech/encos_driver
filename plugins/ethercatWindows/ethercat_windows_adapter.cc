#include "ethercat_windows_adapter.h"

#include "bus/bus.h"
#include "platform/delay.h"
#include "utils/thread_priority.h"

namespace encos {

std::unordered_map<int, Bus*> EthercatWindowsAdapter::GetBuses() {
    std::unordered_map<int, Bus*> buses;
    auto bus_sizes_ = ec_master_->GetBusSizes();
    for (int slave_idx = 0; slave_idx < static_cast<int>(bus_sizes_.size()); ++slave_idx) {
        int bus_size = bus_sizes_[slave_idx];
        for (int bus_idx = 0; bus_idx < bus_size; ++bus_idx) {
            auto bus = GetBus(slave_idx, bus_idx);
            buses[bus->GetBusIndex()] = bus;
        }
    }
    return buses;
}

bool EthercatWindowsAdapter::Ok() {
    return true;
}

void EthercatWindowsAdapter::Send(const MotorMessage& message) {
    ec_master_->Send(message);
}

void EthercatWindowsAdapter::SendSynchronized(const MotorMessages& messages) {
    ec_master_->SendSynchronized(messages);
}

EthercatWindowsAdapter::EthercatWindowsAdapter(const std::string& interface_name,
                                               const std::string& logger_name,
                                               encos::LogLevel log_level)
    : BaseAdapter(interface_name, logger_name, log_level) {
    ec_master_ = std::make_shared<EthercatWindowsHandle>(interface_name, Logger());
    ec_master_->SetReceiveCallback([this](const MotorMessages& messages) {
        this->OnMessage(messages);
    });
    loop_thread_ = std::thread(&EthercatWindowsAdapter::Loop, this);
    platform::SleepFor(std::chrono::milliseconds(100));
}

EthercatWindowsAdapter::~EthercatWindowsAdapter() {
    ec_master_->RequestStop();
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
    ec_master_->Stop();
}

void EthercatWindowsAdapter::Loop() {
    if (!utils::SetCurrentThreadPriority(50)) {
        Logger()->warn("Failed to set Windows EtherCAT loop thread priority");
    }
    ec_master_->Loop();
}

EthercatWindowsAdapter* EthercatWindowsAdapter::Create(const std::string& interface_name,
                                                       const std::string& logger_name,
                                                       encos::LogLevel log_level) {
    return new EthercatWindowsAdapter(interface_name, logger_name, log_level);
}

}  // namespace encos
