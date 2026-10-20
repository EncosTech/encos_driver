// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include "adapter/adapter_extend.h"
#include "external/json.hpp"

namespace encos::ethernet {

/** @brief 网关 CAN 通道容量上限；实际数量由订阅握手报告 */
inline constexpr unsigned kGatewayChannelCount = 8;

/**
 * @brief 网关单路 CAN 的位时序
 *
 * 速率单位为 bit/s；采样点为 0-1 小数，与 Can 插件的 extend 写法保持一致，写入网关报文前
 * 换算成固件要求的千分比整数。成员初始化值即默认值。
 */
struct GatewayCanTiming {
    std::uint32_t nominal_bps = 1000000;
    std::uint32_t data_bps = 5000000;
    double nominal_sample_point = 0.75;
    double data_sample_point = 0.875;
};

/** @brief 一个从站 8 路通道各自的生效时序 */
using GatewayChannelTimings = std::array<GatewayCanTiming, kGatewayChannelCount>;

/**
 * @brief 把 0-1 采样点换算成固件使用的千分比整数
 * @param sample_point 采样点，0-1 小数
 * @return 千分比整数，例如 0.75 -> 750
 */
inline std::uint32_t ToPerMille(double sample_point) {
    return static_cast<std::uint32_t>(sample_point * 1000.0 + 0.5);
}

/** @brief 本插件认识的字段名，与 Can 插件统一 */
inline constexpr const char* kGatewayTimingFields[] = {"bitrate", "sample_point", "dbitrate",
                                                       "dsample_point"};

/**
 * @brief 判断字段名是否为已知的位时序字段
 * @param key 字段名
 * @return 是否为已知字段
 */
inline bool IsGatewayTimingField(const std::string& key) {
    for (const char* field : kGatewayTimingFields) {
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
        throw std::runtime_error(std::string("Ethernet extend '") + key + "' must be an integer");
    }
    const auto parsed = value.get<long long>();
    if (parsed <= 0 || parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string("Ethernet extend '") + key +
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
        throw std::runtime_error(std::string("Ethernet extend '") + key + "' must be a number");
    }
    const double parsed = value.get<double>();
    if (!(parsed > 0.0 && parsed <= 1.0)) {
        throw std::runtime_error(std::string("Ethernet extend '") + key +
                                 "' must be a sample point in (0, 1]");
    }
    return parsed;
}

}  // namespace detail

/**
 * @brief 把某条总线的生效作用域覆盖到网关位时序上
 * @param scope 由 `AdapterExtend::Scope(slave, channel)` 给出的生效作用域
 * @param timing 待覆盖的位时序；成员当前取值即默认值
 * @throws std::runtime_error 出现本插件不认识的字段，或字段类型/取值非法
 */
inline void ApplyGatewayCanTiming(const nlohmann::json& scope, GatewayCanTiming* timing) {
    if (timing == nullptr) {
        throw std::invalid_argument("ApplyGatewayCanTiming requires a non-null timing target");
    }
    for (const auto& [key, value] : scope.items()) {
        if (!IsGatewayTimingField(key)) {
            throw std::runtime_error("Ethernet extend field '" + key + "' is not supported");
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
 * @brief 按从站实际通道数生成时序，并拒绝越界覆盖
 * @param extend 已解析的扩展配置信封
 * @param slave 从站号
 * @param channel_count 实际通道数，范围 1–8
 * @return 逐通道生效时序
 * @throws std::runtime_error 任一通道的作用域含非法字段或取值
 */
inline GatewayChannelTimings BuildGatewayChannelTimings(
    const AdapterExtend& extend, int slave, unsigned channel_count = kGatewayChannelCount) {
    if (channel_count == 0 || channel_count > kGatewayChannelCount) {
        throw std::runtime_error("Ethernet gateway reports invalid CAN channel count");
    }
    for (const auto& bus : extend.DeclaredBuses()) {
        if (bus.first == slave && static_cast<unsigned>(bus.second) >= channel_count) {
            throw std::runtime_error("Ethernet adapter has no bus '" + std::to_string(slave) + ":" +
                                     std::to_string(bus.second) + "'");
        }
    }
    GatewayChannelTimings timings;
    for (unsigned channel = 0; channel < channel_count; ++channel) {
        ApplyGatewayCanTiming(extend.Scope(slave, static_cast<int>(channel)), &timings[channel]);
    }
    return timings;
}

}  // namespace encos::ethernet
