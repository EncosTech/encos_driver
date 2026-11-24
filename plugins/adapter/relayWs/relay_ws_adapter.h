// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>

#include "adapter/adapter_plugin.h"
#include "adapter/base_adapter.h"

namespace encos {

class RelayWsAdapter : public BaseAdapter {
public:
    RelayWsAdapter(const std::string& interface_name, const std::string& logger_name,
                   LogLevel log_level, const std::string& extend_json = "");
    ~RelayWsAdapter() override;

    std::unordered_map<int, Bus*> GetBuses() override;
    bool Ok() override;

protected:
    void Send(const MotorMessage& message) override;
    void SendSynchronized(const MotorMessages& messages) override;

private:
    void WorkerLoop();
    void WorkerLoopIteration();
    void ReportDroppedIncomingFrames(std::chrono::steady_clock::time_point now);
    static void OnTimer(void* arg);
    void FlushSendBuffer();
    void ResetWireSession();
    bool ConnectSession();
    bool ConnectSessionWithRetry(std::chrono::milliseconds timeout);
    bool WaitForConnection(std::chrono::milliseconds timeout);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

BaseAdapter* CreateRelayWsAdapterStatic(const std::string& interface_name,
                                        const std::string& logger_name, LogLevel log_level);

/** @brief 创建带连接级协议扩展配置的 RelayWs 适配器 */
BaseAdapter* CreateRelayWsAdapterStaticWithExtend(const std::string& interface_name,
                                                  const std::string& logger_name,
                                                  LogLevel log_level,
                                                  const std::string& extend_json);

/**
 * @brief RelayWs 由远端地址指定接口，枚举结果为空
 * @param emit 接口标识回调
 * @param context 调用方上下文
 */
void EnumerateRelayWsInterfaces(AdapterInterfaceEmit emit, void* context);

}  // namespace encos
