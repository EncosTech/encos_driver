// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ethernet/protocol/ipv4.h"
namespace encos::ethernet {
/** @brief 已连接的非阻塞UDP通道；协议层不持有平台套接字。 */
class DatagramChannel {
public:
    virtual ~DatagramChannel() = default;
    /** @brief 原子发送报文，暂时无缓冲空间时返回false。 */
    virtual bool Send(const uint8_t* data, std::size_t size) = 0;
    /** @brief 接收报文，无数据或报文超长返回0。 */
    virtual std::size_t Receive(uint8_t* data, std::size_t capacity) = 0;
    /** @brief 等待可读，超时返回false。 */
    virtual bool Wait(int milliseconds) = 0;
};
/** @brief 在指定网卡建立连接并固定远端IP和UDP端口。 */
std::unique_ptr<DatagramChannel> ConnectDatagram(const Endpoint& endpoint);
/** @brief 广播接收结果，保留入接口与源地址供协议层验证。 */
struct DiscoveryDatagram {
    std::string interface_name;
    std::string address;
    uint16_t port = 0;
    std::vector<uint8_t> payload;
};
/** @brief 平台广播收发接口；发送和接收只使用选中的网卡。 */
class DiscoveryChannel {
public:
    virtual ~DiscoveryChannel() = default;
    virtual void Broadcast(const uint8_t* data, std::size_t size) = 0;
    virtual DiscoveryDatagram Receive(int milliseconds) = 0;
};
/** @brief 创建指定网卡的广播通道，空名称表示所有可用IPv4网卡。 */
std::unique_ptr<DiscoveryChannel> OpenDiscovery(const std::string& interface_name);
/** @brief 管理线程使用普通调度，避免继承数据线程的实时优先级。 */
bool SetManagementThreadScheduling();
}  // namespace encos::ethernet
