#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <soem/soem.h>
#include <string>
#include <vector>

#include "ethercat_base_handle.h"

class EthercatHandle final : public EthercatBaseHandle {
public:
    static constexpr auto kDefaultLoopPeriod = std::chrono::microseconds(1000);

#ifdef ENCOS_STATIC_MODE
    EthercatHandle(std::string ifname, LoggerPtr logger);
#else
    EthercatHandle(std::string ifname, std::string broker_executable, LoggerPtr logger);
#endif
    ~EthercatHandle();

    void Send(const MotorMessage& message);
    void Send(const MotorMessages& messages);
    void SendSynchronized(const MotorMessages& messages);
    void Loop(std::chrono::microseconds period = kDefaultLoopPeriod);

    EthercatHandle(const EthercatHandle&) = delete;
    EthercatHandle& operator=(const EthercatHandle&) = delete;
    void RequestStop();
    void Stop();
    /** @brief 仅在运行且已确认 OP 与有效过程数据时返回 true */
    bool Ok() const;

private:
    friend class EthercatHandleTestAccess;
    explicit EthercatHandle(LoggerPtr logger) : EthercatBaseHandle(std::move(logger)) {}
    bool Initialize();
    void CloseContext();
    bool TransitionToOperational();
    void CheckState();
    void ExchangeOnce();
    void DegradedHandler();
    void LogBadWkc();
    void ResetBadWkcLogState();

    bool SetupPortFromFd(ecx_portt* port, int socket_fd);
    int RequestSocketFromBroker();

    void WriteOutputs(const OutputFrame& packets);
    MotorMessages ReadInputs();

    std::string ifname_;
#ifndef ENCOS_STATIC_MODE
    std::string broker_executable_;
#endif
    ecx_contextt ctx_{};
    std::vector<char> io_map_;
    std::atomic<int> expected_wkc_{0};
    std::atomic<int> wkc_{0};
    uint8_t current_group_{0};
    std::chrono::steady_clock::time_point last_bad_wkc_log_{};
    std::size_t suppressed_bad_wkc_logs_{0};
    bool bad_wkc_log_active_{false};
    std::atomic<bool> context_closed_{true};
    /** @brief 将发送入队与主动停止串行化，防止恢复检查撤销停止状态 */
    platform::Mutex recovery_mutex_;
    bool slaves_operational_{false};
    /** @brief SOEM 调用边界；生产使用默认实现，测试可替换为确定性链路模拟 */
    struct IoOperations {
        decltype(&ecx_readstate) read_state = ecx_readstate;
        decltype(&ecx_writestate) write_state = ecx_writestate;
        decltype(&ecx_statecheck) state_check = ecx_statecheck;
        decltype(&ecx_reconfig_slave) reconfigure = ecx_reconfig_slave;
        decltype(&ecx_recover_slave) recover = ecx_recover_slave;
        decltype(&ecx_send_processdata) send = ecx_send_processdata;
        decltype(&ecx_receive_processdata) receive = ecx_receive_processdata;
    } io_;
};
