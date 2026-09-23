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
#include "platform/log.h"
#include "protocol/management_protocol.h"
namespace encos::ethernet {
class DatagramChannel;
class DiscoveryChannel;
/** @brief 发现指定网卡上固定/24网段的从站，冲突ID抛出异常。 */
std::vector<Endpoint> DiscoverSlaves(const std::string& interface_name);
/** @brief 在已有广播通道上执行发现与身份校验，可用于模拟网关测试。 */
std::vector<Endpoint> DiscoverSlaves(DiscoveryChannel& channel, const std::string& interface_name);
/** @brief 在已连接的管理UDP套接字上配置8路CAN为1M/5M、75%/87.5%；拒绝或超时抛异常。 */
std::array<uint8_t, 16> InitializeCanChannels(
    DatagramChannel& socket, const std::function<void(const CanError&)>& on_event = {});
/** @brief 独立普通调度线程订阅管理UDP并记录CAN错误，不阻塞AF_XDP数据线程。 */
class ManagementClient {
public:
    ManagementClient(const Endpoint& endpoint, LoggerPtr logger,
                     std::function<bool(unsigned)> has_devices = {});
    ~ManagementClient();
    ManagementClient(const ManagementClient&) = delete;
    ManagementClient& operator=(const ManagementClient&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace encos::ethernet
