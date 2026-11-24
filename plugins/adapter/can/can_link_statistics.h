// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "adapter/adapter_statistics.h"

namespace encos::can {

/** @brief 解码内核 RTM_NEWLINK 消息中的当前 CAN 发送及接收错误计数。 */
std::optional<BusStatistics> DecodeLinkStatistics(const void* data, std::size_t size, int index);

/** @brief 通过只读 rtnetlink 查询接口，等待上限 50ms，不依赖电机帧过滤器。 */
std::optional<BusStatistics> ReadLinkStatistics(const std::string& interface_name);

}  // namespace encos::can
