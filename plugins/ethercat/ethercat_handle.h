// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: GPL-3.0-or-later

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

    EthercatHandle(std::string ifname, LoggerPtr logger);
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
    /** @brief 等待启动循环确认从站处于 OP 且过程数据 WKC 有效 */
    bool WaitUntilOperational(std::chrono::milliseconds timeout) const;

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

    /** @brief 使用当前平台的网络后端打开 SOEM 端口 */
    bool OpenPort();

    void WriteOutputs(const OutputFrame& packets);
    MotorMessages ReadInputs();

    std::string ifname_;
    ecx_contextt ctx_{};
    std::vector<char> io_map_;
    std::atomic<int> expected_wkc_{0};
    std::atomic<int> wkc_{0};
    std::atomic<int> wkc_error_count_{0};
    std::atomic<int> wkc_error_iteration_{0};
    uint8_t current_group_{0};
    std::atomic<bool> context_closed_{true};
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
