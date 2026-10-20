// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

#include "adapter/adapter_extend.h"
#include "platform/log.h"

namespace encos::adapter_update {

/**
 * @brief 报告并忽略增量更新配置中的顶层公共字段
 * @param extend 已解析的扩展配置信封
 * @param logger 适配器自己的日志记录器；为空时不记录
 * @return 确实存在被忽略的顶层字段时返回 true
 *
 * 增量更新只处理 `buses` 中显式点名的总线。顶层公共参数作用于全部总线，无法在既有实例上
 * 重新应用——那会连带改掉其他消费者已经在用的总线——因此这里只做诊断，不报错，以免把
 * 「同一份信封重复喂给每个消费者」这种既有用法变成硬失败。
 */
inline bool WarnIgnoredCommonFields(const AdapterExtend& extend, const LoggerPtr& logger) {
    if (extend.Common().empty()) {
        return false;
    }
    if (logger == nullptr) {
        return true;
    }
    std::string keys;
    for (const auto& [key, value] : extend.Common().items()) {
        if (!keys.empty()) {
            keys += ", ";
        }
        keys += key;
    }
    logger->warn(
        "bus timing update ignores top-level fields [{}]; only entries under 'buses' "
        "reconfigure an existing adapter",
        keys);
    return true;
}

}  // namespace encos::adapter_update
