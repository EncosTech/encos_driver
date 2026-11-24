// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once
#include <memory>

#include "ethernet/protocol/ipv4.h"
#include "utils/emr2/relay_frame.h"
namespace encos::ethernet {
/** @brief 数据通道：Linux使用AF_XDP，Windows使用Winsock UDP。 */
class Transport {
public:
    virtual ~Transport() = default;
    virtual bool Send(unsigned slave, const MotorMessages& messages) = 0;
    /** @brief 发送一个已编码的数据报；失败时调用方只重试当前报文。 */
    virtual bool SendPayload(unsigned slave, const std::vector<uint8_t>& payload) {
        const auto frames = DecodeRelayPackets(payload);
        if (!frames || frames->size() != 1 || frames->front().protocol != RelayProtocol::Emr1)
            return false;
        MotorMessages messages;
        for (const auto& record : frames->front().records)
            messages.push_back(record.message);
        return Send(slave, messages);
    }
    virtual MotorMessages Receive() = 0;
    virtual void Wait(int milliseconds) = 0;
    virtual void Notify() = 0;
};
/** @brief 配置主机为192.168.100.254/24并启用指定网卡。 */
class HostInterface {
public:
    virtual ~HostInterface() = default;
    virtual void ValidateRoute(const std::string& destination) const = 0;
};
std::unique_ptr<HostInterface> ConfigureHostInterface(const std::string& interface_name,
                                                      bool exclusive = false);
/** @brief 创建数据通道；multi决定总线索引是否带从站ID。 */
std::unique_ptr<Transport> CreateTransport(const std::vector<Endpoint>& endpoints, bool multi);
}  // namespace encos::ethernet
