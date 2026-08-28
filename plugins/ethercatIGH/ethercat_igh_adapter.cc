#include "ethercat_igh_adapter.h"

#include <chrono>
#include <mutex>

#include "bus/bus.h"
#include "platform/delay.h"
#include "utils/thread_priority.h"

namespace encos {

std::unordered_map<int, Bus*> EthercatIGHAdapter::GetBuses() {
    // 根据 handle 返回的每个 slave 的 bus 数量，生成稳定的 bus 索引。
    std::unordered_map<int, Bus*> buses;
    auto bus_sizes = ec_master_->GetBusSizes();
    for (int slave_idx = 0; slave_idx < static_cast<int>(bus_sizes.size()); ++slave_idx) {
        for (int bus_idx = 0; bus_idx < bus_sizes[slave_idx]; ++bus_idx) {
            auto bus = GetBus(slave_idx, bus_idx);
            buses[bus->GetBusIndex()] = bus;
        }
    }
    return buses;
}

bool EthercatIGHAdapter::Ok() {
    // 与 EthercatWindowsAdapter 保持一致：实例存在即认为可用。
    return ec_master_ != nullptr;
}

void EthercatIGHAdapter::Send(const MotorMessage& message) {
    // BaseAdapter 单条发送入口，包装为批量消息交由 handle 入队。
    ec_master_->Send(message);
}

void EthercatIGHAdapter::Send(const MotorMessages& messages) {
    ec_master_->Send(messages);
}

void EthercatIGHAdapter::SendSynchronized(const MotorMessages& messages) {
    ec_master_->SendSynchronized(messages);
}

EthercatIGHAdapter::EthercatIGHAdapter(const std::string& interface_name,
                                       const std::string& logger_name, encos::LogLevel log_level)
    : BaseAdapter(interface_name, logger_name, log_level) {
    // 直连模式：构造后直接创建 IGH handle，并将回调绑定到 BaseAdapter::OnMessage。
    ec_master_ = std::make_shared<EthercatIGHHandle>(interface_name, Logger());
    ec_master_->SetReceiveCallback([this](const MotorMessages& messages) {
        this->OnMessage(messages);
    });
    loop_thread_ = std::thread(&EthercatIGHAdapter::Loop, this);
    platform::SleepFor(std::chrono::milliseconds(100));
}

EthercatIGHAdapter::~EthercatIGHAdapter() {
    // 先请求循环退出，再等待线程回收，最后释放 master，避免析构竞态。
    if (ec_master_) {
        ec_master_->Stop();
    }
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
    if (ec_master_) {
        ec_master_->Release();
    }
}

void EthercatIGHAdapter::Loop() {
    // 单独线程运行 IGH 周期收发。
    if (!utils::SetCurrentThreadPriority(50)) {
        Logger()->warn("Failed to set IGH EtherCAT loop thread priority");
    }
    ec_master_->Loop();
}

EthercatIGHAdapter* EthercatIGHAdapter::Create(const std::string& interface_name,
                                               const std::string& logger_name,
                                               encos::LogLevel log_level) {
    return new EthercatIGHAdapter(interface_name, logger_name, log_level);
}

}  // namespace encos
