#pragma once

#include <chrono>

#include "adapter/base_adapter.h"

namespace encos {

class RelayWsAdapter : public BaseAdapter {
public:
    RelayWsAdapter(const std::string& interface_name, const std::string& logger_name,
                   LogLevel log_level);
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
    bool ConnectSession();
    bool ConnectSessionWithRetry(std::chrono::milliseconds timeout);
    bool WaitForConnection(std::chrono::milliseconds timeout);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

BaseAdapter* CreateRelayWsAdapterStatic(const std::string& interface_name,
                                        const std::string& logger_name, LogLevel log_level);

}  // namespace encos
