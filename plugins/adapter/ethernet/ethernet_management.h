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
/** @brief 独立普通调度线程订阅管理UDP并记录CAN错误，不阻塞AF_XDP数据线程。 */
class ManagementClient {
public:
    ManagementClient(const Endpoint& endpoint, LoggerPtr logger, const TimingFactory& make_timings,
                     std::function<bool(unsigned)> has_devices = {});
    ~ManagementClient();
    /** @brief 返回已握手并完成初始化的设备能力和时序 */
    const GatewayInitialization& Initialization() const;
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
