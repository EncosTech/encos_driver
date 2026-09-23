// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstddef>
#include <memory>
#include <string>

#include "xdp_shared.h"

namespace encos::ethernet {
/** @brief 静态模式检查当前进程权限；缺失时直接报错，不触发提权。 */
void RequireXdpProcessCapabilities();
/** @brief 在当前进程中设置网卡地址、路由并启用链路，调用方需有网络管理权限。 */
void ConfigureXdpInterfaceDirect(const char* name);
/** @brief 拥有 AF_XDP、UMEM 和 BPF 资源；析构时卸载本会话的过滤器。 */
class XdpSession {
public:
    XdpSession();
    ~XdpSession();
    XdpSession(const XdpSession&) = delete;
    XdpSession& operator=(const XdpSession&) = delete;
    /** @brief 从外部 BPF 对象初始化会话，供动态模式 broker 使用；仅可初始化一次。 */
    void OpenFile(const encos_xdp_request& request, const std::string& path);
    /** @brief 从嵌入的 BPF 对象初始化会话，供静态模式使用；仅可初始化一次。 */
    void OpenMemory(const encos_xdp_request& request, const unsigned char* data, std::size_t size);
    /** @brief 返回会话持有的 socket，调用者使用副本或借用，不能关闭原描述符。 */
    int SocketFd() const;
    /** @brief 返回会话持有的 UMEM 文件描述符，调用者不能关闭原描述符。 */
    int MemoryFd() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace encos::ethernet
