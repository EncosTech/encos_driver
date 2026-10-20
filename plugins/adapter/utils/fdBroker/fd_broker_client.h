// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "platform/log.h"

namespace encos {
namespace fd_broker {

class FdBrokerClient {
public:
    /**
     * @brief 请求 broker 配置接口并交回 socket 文件描述符
     * @param broker_executable broker 可执行文件路径
     * @param interface_name 传输接口名
     * @param socket_prefix 私有 socket 目录前缀
     * @param logger 日志记录器
     * @param timeout 握手总超时
     * @param extra_arguments 追加在既有参数之后的额外参数，随 argv 一同传给 broker
     * @return 可用的 fd；失败时返回负值
     *
     * 额外参数一律追加在末尾，保持 broker 的 argc 下界校验、nonce 与
     * executable device/inode 身份校验以及自重启的参数转发不变。
     */
    static int RequestFd(const std::string& broker_executable, const std::string& interface_name,
                         const std::string& socket_prefix, LoggerPtr logger,
                         std::chrono::milliseconds timeout = std::chrono::milliseconds(5000),
                         const std::vector<std::string>& extra_arguments = {});
};

}  // namespace fd_broker
}  // namespace encos
