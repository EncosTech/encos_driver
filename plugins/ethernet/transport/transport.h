// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once
#include <memory>

#include "ethernet/protocol/ipv4.h"
namespace encos::ethernet {
/** @brief 数据通道：Linux使用AF_XDP，Windows使用Winsock UDP。 */
class Transport {
public:
    virtual ~Transport() = default;
    virtual bool Send(unsigned slave, const MotorMessages& messages) = 0;
    virtual MotorMessages Receive() = 0;
    virtual void Wait(int milliseconds) = 0;
    virtual void Notify() = 0;
};
/** @brief 配置主机为192.168.100.254/24并启用指定网卡。 */
void ConfigureHostInterface(const std::string& interface_name);
/** @brief 创建数据通道；multi决定总线索引是否带从站ID。 */
std::unique_ptr<Transport> CreateTransport(const std::vector<Endpoint>& endpoints, bool multi);
}  // namespace encos::ethernet
