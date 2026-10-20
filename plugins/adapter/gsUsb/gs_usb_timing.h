// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "adapter/adapter_extend.h"
#include "external/json.hpp"

namespace encos::gs_usb {

/**
 * @brief 单路 gs_usb 通道的 CAN 位时序
 *
 * 字段名与 Can、Ethernet 插件统一（`bitrate`、`sample_point`、`dbitrate`、`dsample_point`），
 * 因为三者配置的都是 CAN 总线时序。成员初始化值即默认值，保持与本插件既有行为一致。
 */
struct ChannelTiming {
    std::uint32_t nominal_bps = 1000000;
    double nominal_sample_point = 0.765;
    std::uint32_t data_bps = 5000000;
    double data_sample_point = 0.882;
};

/** @brief 本插件认识的位时序字段名，与 Can、Ethernet 插件统一 */
inline constexpr const char* kTimingFields[] = {"bitrate", "sample_point", "dbitrate",
                                                "dsample_point"};

/**
 * @brief 判断字段名是否为已知的位时序字段
 * @param key 字段名
 * @return 是否为已知字段
 */
inline bool IsTimingField(const std::string& key) {
    for (const char* field : kTimingFields) {
        if (key == field) {
            return true;
        }
    }
    return false;
}

namespace detail {

/**
 * @brief 读取作用域中的位率字段，缺省时沿用默认值
 * @throws std::runtime_error 字段存在但不是正整数
 */
inline std::uint32_t ReadRate(const nlohmann::json& scope, const char* key,
                              std::uint32_t fallback) {
    if (!scope.contains(key)) {
        return fallback;
    }
    const auto& value = scope.at(key);
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        throw std::runtime_error(std::string("GsUsb extend '") + key + "' must be an integer");
    }
    const auto parsed = value.get<long long>();
    if (parsed <= 0 || parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string("GsUsb extend '") + key +
                                 "' must be a positive bitrate in bit/s");
    }
    return static_cast<std::uint32_t>(parsed);
}

/**
 * @brief 读取作用域中的采样点字段，缺省时沿用默认值
 * @throws std::runtime_error 字段存在但不是数值或超出取值范围
 */
inline double ReadSamplePoint(const nlohmann::json& scope, const char* key, double fallback) {
    if (!scope.contains(key)) {
        return fallback;
    }
    const auto& value = scope.at(key);
    if (!value.is_number()) {
        throw std::runtime_error(std::string("GsUsb extend '") + key + "' must be a number");
    }
    const double parsed = value.get<double>();
    if (!(parsed > 0.0 && parsed <= 1.0)) {
        throw std::runtime_error(std::string("GsUsb extend '") + key +
                                 "' must be a sample point in (0, 1]");
    }
    return parsed;
}

}  // namespace detail

/**
 * @brief 把某条总线的生效作用域覆盖到通道位时序上
 * @param scope 由 `AdapterExtend::Scope(slave, channel)` 给出的生效作用域
 * @param timing 待覆盖的位时序；成员当前取值即默认值
 * @throws std::runtime_error 出现本插件不认识的字段，或字段类型/取值非法
 */
inline void ApplyChannelTiming(const nlohmann::json& scope, ChannelTiming* timing) {
    if (timing == nullptr) {
        throw std::invalid_argument("ApplyChannelTiming requires a non-null timing target");
    }
    for (const auto& [key, value] : scope.items()) {
        if (!IsTimingField(key)) {
            throw std::runtime_error("GsUsb extend field '" + key + "' is not supported");
        }
    }
    timing->nominal_bps = detail::ReadRate(scope, "bitrate", timing->nominal_bps);
    timing->data_bps = detail::ReadRate(scope, "dbitrate", timing->data_bps);
    timing->nominal_sample_point =
        detail::ReadSamplePoint(scope, "sample_point", timing->nominal_sample_point);
    timing->data_sample_point =
        detail::ReadSamplePoint(scope, "dsample_point", timing->data_sample_point);
}

/**
 * @brief 取出设备每个通道各自的生效时序
 *
 * gs_usb 设备的通道直接就是总线，总线标识为 `<0>:<channel>` 或其等价裸索引。顶层公共参数
 * 作用于全部通道，`buses` 中的条目按通道覆盖。
 *
 * @param extend 已解析的扩展配置信封
 * @param channels 设备实际通道数
 * @return 逐通道生效时序
 * @throws std::runtime_error 任一通道的作用域含非法字段或取值
 */
inline std::vector<ChannelTiming> BuildChannelTimings(const AdapterExtend& extend,
                                                      unsigned channels) {
    std::vector<ChannelTiming> timings(channels);
    for (unsigned channel = 0; channel < channels; ++channel) {
        ApplyChannelTiming(extend.Scope(0, static_cast<int>(channel)), &timings[channel]);
    }
    return timings;
}

}  // namespace encos::gs_usb
