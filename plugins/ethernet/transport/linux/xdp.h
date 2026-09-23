// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once
#include <memory>
#include <vector>

#include "ethernet/transport/transport.h"
namespace encos::ethernet {
/** @brief 发现前配置网卡；静态模式使用当前进程权限，动态模式通过辅助进程授权。 */
void ConfigureXdpHostInterface(const std::string& interface_name, const std::string& broker_path);
/** @brief AF_XDP generic/copy 传输；静态模式持有资源，动态模式由辅助进程管理资源。 */
class XdpTransport : public Transport {
public:
    /** @brief 绑定指定网卡队列零并建立板卡 UDP 会话。 */
    XdpTransport(const Endpoint& endpoint, const std::string& broker_path);
    /** @brief 同一网卡的多个已发现从站共享一个AF_XDP队列。 */
    XdpTransport(const std::vector<Endpoint>& endpoints, const std::string& broker_path);
    ~XdpTransport();
    XdpTransport(const XdpTransport&) = delete;
    XdpTransport& operator=(const XdpTransport&) = delete;
    /** @brief 提交一批 CAN 消息，环满时返回 false，由调用者保留待发消息。 */
    bool Send(const MotorMessages& messages);
    /** @brief 发送指定slave的本地CAN索引消息。 */
    bool Send(unsigned slave, const MotorMessages& messages);
    /** @brief 读取一批 CAN 消息并回收 RX 缓冲；只能由同一个工作线程调用。 */
    MotorMessages Receive();
    /** @brief 等待 RX 或短暂超时，避免空闲时持续占满 CPU。 */
    void Wait(int milliseconds);
    /** @brief 从发送线程唤醒收发工作线程。 */
    void Notify();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace encos::ethernet
