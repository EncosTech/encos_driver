#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <soem/soem.h>
#include <string>
#include <thread>
#include <vector>

#include "ethercat_base_handle.h"

class EthercatWindowsHandle final : public EthercatBaseHandle {
public:
    explicit EthercatWindowsHandle(std::string ifname, LoggerPtr logger);
    ~EthercatWindowsHandle();

    void Send(const MotorMessage& message);
    void Send(const MotorMessages& messages);
    void SendSynchronized(const MotorMessages& messages);
    void Loop(std::chrono::microseconds period = std::chrono::microseconds(1000));

    EthercatWindowsHandle(const EthercatWindowsHandle&) = delete;
    EthercatWindowsHandle& operator=(const EthercatWindowsHandle&) = delete;
    void RequestStop();
    void Stop();

private:
    bool Initialize();
    void CloseContext();
    bool TransitionToOperational();
    void CheckLoop();
    void DegradedHandler();
    void LogBadWkc();
    void ResetBadWkcLogState();

    void WriteOutputs(const OutputFrame& packets);
    MotorMessages ReadInputs();

    std::string ifname_;
    ecx_contextt ctx_{};
    std::vector<char> io_map_;
    std::atomic<int> expected_wkc_{0};
    std::atomic<int> wkc_{0};
    uint8_t current_group_{0};
    std::atomic<int> err_count_{0};
    std::atomic<int> err_iteration_{0};
    std::atomic<int> wkc_err_count_{0};
    std::atomic<int> wkc_err_iteration_{0};
    std::chrono::steady_clock::time_point last_bad_wkc_log_{};
    std::size_t suppressed_bad_wkc_logs_{0};
    bool bad_wkc_log_active_{false};
    std::atomic<bool> context_closed_{true};
    std::thread check_thread_;
};
