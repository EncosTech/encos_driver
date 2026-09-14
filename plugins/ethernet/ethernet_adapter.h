#pragma once
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "adapter/base_adapter.h"
#include "ethernet_management.h"
#include "ethernet_tx_queue.h"
#include "transport/transport.h"

namespace encos {
/** @brief 通过平台数据通道和 EMR1/UDP 连接八通道 CAN 网关。 */
class EthernetAdapter : public BaseAdapter {
public:
    /** @brief 只指定网卡名，广播发现slave并使用GetBus(slave_id,can_id)。 */
    EthernetAdapter(const std::string& interface_name, const std::string& logger_name,
                    LogLevel level);
    ~EthernetAdapter() override;
    std::unordered_map<int, Bus*> GetBuses() override;
    bool Ok() override;

protected:
    void Send(const MotorMessage& message) override;
    void SendSynchronized(const MotorMessages& messages) override;

private:
    void Worker();
    std::unique_ptr<ethernet::Transport> transport_;
    struct Peer {
        std::unique_ptr<ethernet::ManagementClient> management;
        ethernet::TransmitQueue pending;
        MotorMessages outgoing;
        std::chrono::steady_clock::time_point heartbeat{};
    };
    bool multi_ = false;
    std::map<unsigned, Peer> peers_;
    std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};
/** @brief 创建 Ethernet 插件适配器。 */
BaseAdapter* CreateEthernetAdapterStatic(const std::string& interface_name,
                                         const std::string& logger_name, LogLevel level);
}  // namespace encos
