// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "can_adapter.h"

#include <chrono>
#include <stdexcept>

#include "adapter/adapter_extend.h"
#include "bus/bus.h"
#include "can_socket_setup.h"
#include "can_timing.h"
#include "platform/delay.h"
#include "utils/thread_priority.h"
#ifndef ENCOS_STATIC_MODE
#include <filesystem>

#include "fd_broker_client.h"
#endif
// EnumerateCanInterfaces 在静态模式下也要用 platform 接口枚举，故无条件包含
#include "platform/os.h"

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
        throw std::runtime_error("Failed to authorize CAN loop thread priority");
    }
#endif
}

}  // namespace

std::unordered_map<int, Bus*> CanAdapter::GetBuses() {
    return {{0, this->GetBus(0)}};
}

bool CanAdapter::Ok() {
    return can_handle_ && can_handle_->Ok();
}

void CanAdapter::Send(const MotorMessage& message) {
    if (!Ok()) {
        Logger()->warn("Adapter not operational, cannot Send message");
        return;
    }
    can_handle_->Send(message);
}

CanAdapter::CanAdapter(const std::string& interface_name, const std::string& logger_name,
                       encos::LogLevel log_level, const can::CanTiming& timing)
    : BaseAdapter(interface_name, logger_name, log_level) {
#ifdef ENCOS_STATIC_MODE
    if (!can::EnsureCanInterfaceReady(interface_name, timing, Logger())) {
        throw std::runtime_error("Failed to initialize CAN interface in static mode");
    }

    can_handle_ = std::make_unique<CanHandle>(interface_name);
#else
    const int socket_fd = RequestSocketFromBroker(timing);
    if (socket_fd < 0) {
        throw std::runtime_error("Failed to Initialize CAN socket from broker");
    }

    can_handle_ = std::make_unique<CanHandle>(socket_fd);
#endif
    can_handle_->SetBatchCallback([this](const MotorMessages& messages) {
        OnMessage(messages);
    });

    RequireLoopPriority();
    running_.store(true);
    loop_thread_ = std::thread(&CanAdapter::Loop, this);
    platform::SleepFor(std::chrono::milliseconds(100));
}

void CanAdapter::Loop() {
    if (!utils::SetCurrentThreadPriority(50)) {
        Logger()->error("Failed to set CAN loop thread priority after authorization");
        return;
    }
    if (can_handle_) {
        can_handle_->Loop();
    }
}

void CanAdapter::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    Logger()->info("Stopping CAN adapter...");
    if (can_handle_) {
        can_handle_->Stop();
    }
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
}

int CanAdapter::RequestSocketFromBroker(const can::CanTiming& timing) const {
#ifdef ENCOS_STATIC_MODE
    return -1;
#else
    auto plugin_dir = platform::PluginDir();
#if defined(_WIN32)
    fs::path broker_path = plugin_dir / "CanFdBrokerExecutable.exe";
#else
    fs::path broker_path = plugin_dir / "CanFdBrokerExecutable";
#endif
    // 位时序在 broker 子进程内生效，作为追加参数随 argv 传过去
    return encos::fd_broker::FdBrokerClient::RequestFd(
        broker_path.string(), GetInterfaceName(), "encos_can_driver", Logger(),
        std::chrono::milliseconds(5000), {can::FormatCanTiming(timing)});
#endif
}

CanAdapter::~CanAdapter() {
    Stop();
}

CanAdapter* CanAdapter::Create(const std::string& interface_name, const std::string& logger_name,
                               encos::LogLevel log_level) {
    return new CanAdapter(interface_name, logger_name, log_level, can::CanTiming{});
}

CanAdapter* CanAdapter::CreateWithExtend(const std::string& interface_name,
                                         const std::string& logger_name, encos::LogLevel log_level,
                                         const std::string& extend_json) {
    const auto extend = encos::AdapterExtend::Parse(extend_json);
    // CAN 适配器只有一条总线 (0, 0)，其余总线条目属于配置错误
    for (const auto& [slave, bus] : extend.DeclaredBuses()) {
        if (slave != 0 || bus != 0) {
            throw std::runtime_error("CAN adapter has no bus '" + std::to_string(slave) + ":" +
                                     std::to_string(bus) + "'");
        }
    }
    can::CanTiming timing;
    can::ApplyCanTiming(extend.Scope(0, 0), &timing);
    return new CanAdapter(interface_name, logger_name, log_level, timing);
}

void EnumerateCanInterfaces(AdapterInterfaceEmit emit, void* context) {
    if (emit == nullptr) {
        return;
    }
    for (const auto& name : platform::GetCanInterfaceNames()) {
        emit(name.c_str(), context);
    }
}

}  // namespace encos
