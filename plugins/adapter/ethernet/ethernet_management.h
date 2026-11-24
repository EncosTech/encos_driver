// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ethernet/protocol/ipv4.h"
#include "ethernet_timing.h"
#include "platform/log.h"
#include "protocol/management_protocol.h"
#include "utils/emr2/relay_frame.h"

namespace encos::ethernet {

class DatagramChannel;
class DiscoveryChannel;

/** @brief 发现指定网卡上固定/24网段的从站，冲突ID抛出异常。 */
std::vector<Endpoint> DiscoverSlaves(const std::string& interface_name);

/** @brief 在已有广播通道上执行发现与身份校验，可用于模拟网关测试。 */
std::vector<Endpoint> DiscoverSlaves(DiscoveryChannel& channel, const std::string& interface_name);

/** @brief 完成握手及 CAN 配置后的身份、实际通道数和生效时序 */
struct GatewayInitialization {
    std::array<uint8_t, 16> identity{};
    unsigned channel_count = 0;
    GatewayChannelTimings timings{};
};

/** @brief 按握手通道数构造并校验时序，在任何 CAN_SET 下发前调用 */
using TimingFactory = std::function<GatewayChannelTimings(unsigned)>;

/** @brief 订阅并配置实际 CAN 通道；失败时取消订阅并抛异常 */
GatewayInitialization InitializeCanChannels(
    DatagramChannel& socket, const TimingFactory& make_timings,
    const std::function<void(const CanError&)>& on_event = {});

/** @brief 使用给定时序配置实际 CAN 通道；数组中超出设备通道数的容量不下发 */
GatewayInitialization InitializeCanChannels(
    DatagramChannel& socket, const GatewayChannelTimings& timings = GatewayChannelTimings{},
    const std::function<void(const CanError&)>& on_event = {});

/** @brief 只读能力探测；200ms 内未收到匹配响应时保守返回 EMR1。 */
RelayCapabilities ProbeRelayCapabilities(DatagramChannel& socket,
                                         const std::array<uint8_t, 16>& identity);

/** @brief 对运行时间回退重新关联探测，旧心跳不直接撤销能力；预算缩小亦视为失效。 */
bool RecheckRelayCapabilities(DatagramChannel& socket, const std::array<uint8_t, 16>& identity,
                              const RelayCapabilities& confirmed, uint32_t previous_uptime,
                              uint32_t current_uptime);

/** @brief 独立普通调度线程订阅管理UDP并记录CAN错误，不阻塞AF_XDP数据线程。 */
class ManagementClient {
public:
    ManagementClient(const Endpoint& endpoint, LoggerPtr logger, const TimingFactory& make_timings,
                     std::function<bool(unsigned)> has_devices = {},
                     std::function<void(const CanError&)> on_statistics = {}, bool start = true,
                     std::function<bool()> probe_capabilities = {});

    /** @brief 启动管理接收线程，仅调用一次。 */
    void Start();

    ~ManagementClient();

    /** @brief 返回已握手并完成初始化的设备能力和时序 */
    const GatewayInitialization& Initialization() const;

    /**
     * @brief 返回当前会话已协商的中继协议能力。
     * @throws std::runtime_error 管理通道超时或能力复核失败，需要重建适配器。
     */
    RelayCapabilities Capabilities() const;

    /** @brief 重新核验期间暂停数据发送，避免误把旧心跳当作协议降级。 */
    bool CapabilitiesReady() const;
    ManagementClient(const ManagementClient&) = delete;

    /**
     * @brief 把一条通道改到给定时序，并等待管理线程执行完成
     * @param channel CAN 通道号
     * @param timing 目标位时序
     * @throws std::runtime_error 网关拒绝该时序，或管理线程未在限期内执行
     *
     * 执行发生在管理线程上：同一个 UDP 套接字只有一个读者。
     */
    void ReconfigureChannel(unsigned channel, const GatewayCanTiming& timing);
    ManagementClient& operator=(const ManagementClient&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos::ethernet
