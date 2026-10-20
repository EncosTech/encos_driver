// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <limits>
#include <stdexcept>
#include <string>

#include "external/json.hpp"

namespace encos {

/**
 * @brief 标准位时序字段的标准默认值
 *
 * SLCAN 是 classic CAN、文本协议没有采样点或数据段概念，因此这三个字段"认识但无法生效"：
 * 取值等于标准默认值时容忍并忽略，取任何其它值时报错。这样一份全用标准默认值的信封可以
 * 原样喂给 CAN 与 SLCAN，而不必按适配器改写。
 */
inline constexpr double kStandardSamplePoint = 0.765;
inline constexpr int kStandardDataBitrate = 5000000;
inline constexpr double kStandardDataSamplePoint = 0.882;

/**
 * @brief 计算 SLCAN 适配器该使用的速率码
 * @param scope 由 `AdapterExtend::Scope(slave, bus)` 给出的生效作用域
 * @return 形如 `S8` 的速率码
 * @throws std::runtime_error 出现无法生效的字段且取值非标准默认值、出现不认识的字段、
 *         `bitrate` 类型非法，或位率无法映射到 `S0`-`S8`
 *
 * 唯一识别的可生效字段是 `bitrate`，缺省为 1000000（`S8`），也是本函数唯一的默认值来源。
 * 串口链路波特率固定 115200，不开放配置。
 */
inline std::string ApplySlcanRateCode(const nlohmann::json& scope) {
    const auto require_standard_default = [](const std::string& key, const nlohmann::json& value,
                                             double standard) {
        if (value.is_number() && value.get<double>() == standard) {
            return;
        }
        throw std::runtime_error("SLCAN does not support extend field '" + key +
                                 "'; only its standard default value is tolerated");
    };

    for (const auto& [key, value] : scope.items()) {
        if (key == "sample_point") {
            require_standard_default(key, value, kStandardSamplePoint);
        } else if (key == "dbitrate") {
            require_standard_default(key, value, static_cast<double>(kStandardDataBitrate));
        } else if (key == "dsample_point") {
            require_standard_default(key, value, kStandardDataSamplePoint);
        } else if (key != "bitrate") {
            throw std::runtime_error("SLCAN extend field '" + key + "' is not supported");
        }
    }

    constexpr int kDefaultBitrate = 1000000;
    int bitrate = kDefaultBitrate;
    if (scope.contains("bitrate")) {
        const auto& value = scope.at("bitrate");
        if (!value.is_number_integer() && !value.is_number_unsigned()) {
            throw std::runtime_error("SLCAN extend 'bitrate' must be an integer");
        }
        const auto parsed = value.get<long long>();
        if (parsed <= 0 || parsed > std::numeric_limits<int>::max()) {
            throw std::runtime_error("SLCAN extend 'bitrate' must be a positive bitrate in bit/s");
        }
        bitrate = static_cast<int>(parsed);
    }

    switch (bitrate) {
        case 10000:
            return "S0";
        case 20000:
            return "S1";
        case 50000:
            return "S2";
        case 100000:
            return "S3";
        case 125000:
            return "S4";
        case 250000:
            return "S5";
        case 500000:
            return "S6";
        case 800000:
            return "S7";
        case 1000000:
            return "S8";
        default:
            throw std::runtime_error("SLCAN does not support bitrate " + std::to_string(bitrate) +
                                     "; supported rates map to S0-S8");
    }
}

}  // namespace encos
