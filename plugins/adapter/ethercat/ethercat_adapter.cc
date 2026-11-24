// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ethercat_adapter.h"

#include <stdexcept>

#include "bus/bus.h"
#include "platform/os.h"
#include "utils/adapter_extend_update.h"
#include "utils/thread_priority.h"
#include "utils/worker_startup.h"

namespace encos {

namespace {

constexpr auto kStartupReadyTimeout = std::chrono::seconds(2);

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
    return running_.load() && ec_master_ && ec_master_->Ok();
}

bool EthercatAdapter::UpdateBusExtend(const std::string& extend_json) {
    if (ec_master_ == nullptr) {
        throw std::runtime_error("Ethercat adapter has no master; bus timing not updated");
    }
    const auto extend = AdapterExtend::Parse(extend_json);
    ethercat::RejectCyclePeriodUpdate(extend);
    adapter_update::WarnIgnoredCommonFields(extend, Logger());
    return ec_master_->ReconfigureBuses(extend);
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
                                 encos::LogLevel log_level, const AdapterExtend& extend)
    : BaseAdapter(interface_name, logger_name, log_level) {
    ec_master_ = std::make_shared<EthercatHandle>(interface_name, Logger(), extend);
    ec_master_->SetReceiveCallback([this](const MotorMessages& messages) {
        this->OnMessage(messages);
    });
    bus_sizes_ = ec_master_->GetBusSizes();

    running_.store(true);

    try {
        adapter_worker::Start(
            loop_thread_,
            [] {
                utils::RequireCurrentThreadPriority(50);
            },
            [this] {
                Loop();
            });

        if (!ec_master_->WaitUntilOperational(kStartupReadyTimeout))
            throw std::runtime_error("EtherCAT startup did not reach operational state");
    } catch (...) {
        Stop();
        throw;
    }
}

void EthercatAdapter::Loop() {
    if (ec_master_)
        ec_master_->Loop();
}

void EthercatAdapter::Stop() {
    running_.store(false);

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

EthercatAdapter* EthercatAdapter::CreateWithExtend(const std::string& interface_name,
                                                   const std::string& logger_name,
                                                   encos::LogLevel log_level,
                                                   const std::string& extend_json) {
    return new EthercatAdapter(interface_name, logger_name, log_level,
                               AdapterExtend::Parse(extend_json));
}

BaseAdapter* CreateEthercatAdapterStaticWithExtend(const std::string& interface_name,
                                                   const std::string& logger_name, LogLevel level,
                                                   const std::string& extend_json) {
    return EthercatAdapter::CreateWithExtend(interface_name, logger_name, level, extend_json);
}

void EnumerateEthercatInterfaces(AdapterInterfaceEmit emit, void* context) {
    if (emit == nullptr) {
        return;
    }
    for (const auto& name : platform::GetWiredInterfaceNames()) {
        emit(name.c_str(), context);
    }
}

}  // namespace encos
