#include "can_adapter.h"

#include <chrono>
#include <stdexcept>

#include "bus/bus.h"
#include "can_socket_setup.h"
#include "platform/delay.h"
#include "utils/thread_priority.h"
#ifndef ENCOS_STATIC_MODE
#include <filesystem>

#include "fd_broker_client.h"
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
                       encos::LogLevel log_level)
    : BaseAdapter(interface_name, logger_name, log_level) {
#ifdef ENCOS_STATIC_MODE
    if (!can::EnsureCanInterfaceReady(interface_name, Logger())) {
        throw std::runtime_error("Failed to initialize CAN interface in static mode");
    }

    can_handle_ = std::make_unique<CanHandle>(interface_name);
#else
    const int socket_fd = RequestSocketFromBroker();
    if (socket_fd < 0) {
        throw std::runtime_error("Failed to Initialize CAN socket from broker");
    }

    can_handle_ = std::make_unique<CanHandle>(socket_fd);
#endif
    can_handle_->SetCallback([this](const MotorMessage& msg) {
        OnMessage(MotorMessages{msg});
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

int CanAdapter::RequestSocketFromBroker() const {
#ifdef ENCOS_STATIC_MODE
    return -1;
#else
    auto plugin_dir = platform::PluginDir();
#if defined(_WIN32)
    fs::path broker_path = plugin_dir / "CanFdBrokerExecutable.exe";
#else
    fs::path broker_path = plugin_dir / "CanFdBrokerExecutable";
#endif
    return encos::fd_broker::FdBrokerClient::RequestFd(broker_path.string(), GetInterfaceName(),
                                                       "encos_can_driver", Logger());
#endif
}

CanAdapter::~CanAdapter() {
    Stop();
}

CanAdapter* CanAdapter::Create(const std::string& interface_name, const std::string& logger_name,
                               encos::LogLevel log_level) {
    return new CanAdapter(interface_name, logger_name, log_level);
}

}  // namespace encos
