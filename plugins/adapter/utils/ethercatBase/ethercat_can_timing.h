// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "adapter/adapter_extend.h"

namespace encos::ethercat {

/** @brief 网关支持的 CAN 通道数量上限 */
inline constexpr unsigned kGatewayChannelCount = 8;

/** @brief 网关单路 CAN 位时序对象 0x2300 + channel 的子索引布局 */
inline constexpr std::uint16_t kCanTimingSdoBase = 0x2300;
inline constexpr std::uint8_t kCanTimingSubNominalBps = 1;
inline constexpr std::uint8_t kCanTimingSubDataBps = 2;
inline constexpr std::uint8_t kCanTimingSubNominalSamplePoint = 3;
inline constexpr std::uint8_t kCanTimingSubDataSamplePoint = 4;
inline constexpr std::uint8_t kCanTimingSubApply = 5;
inline constexpr std::uint8_t kCanTimingSubStatus = 6;

/**
 * @brief 网关单路 CAN 的位时序
 *
 * 速率单位为 bit/s；采样点为 0-1 小数，与 Can/Ethernet 插件的 extend 写法一致，写入
 * 从站前换算成固件要求的千分比整数。成员初始化值即默认值。
 *
 * 本类型与 ethernet::GatewayCanTiming 语义相同：字段名、默认值与换算规则一一对应，只
 * 是传输方式不同（Ethernet 走 EMM1 CAN_SET，本插件走 CoE SDO）。
 */
struct CanTiming {
    std::uint32_t nominal_bps = 1000000;
    std::uint32_t data_bps = 5000000;
    double nominal_sample_point = 0.75;
    double data_sample_point = 0.875;
};

/** @brief 一个从站 8 路通道各自的生效时序 */
using ChannelTimings = std::array<CanTiming, kGatewayChannelCount>;

/**
 * @brief 一个从站实际通道各自需要下发的时序
 *
 * `std::nullopt` 表示该通道未在 extend 中给出任何时序字段，保持固件自身的上电默认，
 * 不下发任何 SDO。
 */
using ChannelTimingPlan = std::vector<std::optional<CanTiming>>;

/**
 * @brief 把 0-1 采样点换算成固件使用的千分比整数
 * @param sample_point 采样点，0-1 小数
 * @return 千分比整数，例如 0.75 -> 750
 */
inline std::uint32_t ToPerMille(double sample_point) {
    return static_cast<std::uint32_t>(sample_point * 1000.0 + 0.5);
}

/** @brief 本插件认识的字段名，与 Can/Ethernet 插件统一 */
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
        throw std::runtime_error(std::string("Ethercat extend '") + key + "' must be an integer");
    }
    const auto parsed = value.get<long long>();
    if (parsed <= 0 || parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string("Ethercat extend '") + key +
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
        throw std::runtime_error(std::string("Ethercat extend '") + key + "' must be a number");
    }
    const double parsed = value.get<double>();
    if (!(parsed > 0.0 && parsed <= 1.0)) {
        throw std::runtime_error(std::string("Ethercat extend '") + key +
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
inline void ApplyGatewayCanTiming(const nlohmann::json& scope, CanTiming* timing) {
    if (timing == nullptr) {
        throw std::invalid_argument("ApplyGatewayCanTiming requires a non-null timing target");
    }
    for (const auto& [key, value] : scope.items()) {
        if (!IsGatewayTimingField(key)) {
            throw std::runtime_error("Ethercat extend field '" + key + "' is not supported");
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
 * @brief 某条总线的生效作用域是否显式给出了位时序字段
 *
 * 未给出任何时序字段的通道不下发 SDO，保持固件自身的上电默认，避免每次创建适配器都
 * 产生 8×5 次写入，也避免插件与固件默认值耦合。
 *
 * @param scope 由 `AdapterExtend::Scope(slave, channel)` 给出的生效作用域
 * @return 作用域含至少一个位时序字段时返回 true
 */
inline bool HasGatewayCanTiming(const nlohmann::json& scope) {
    return scope.is_object() && !scope.empty();
}

/**
 * @brief 取出某从站 8 路通道各自的生效时序
 * @param extend 已解析的扩展配置信封
 * @param slave 从站号（与 `buses` 键的 slave 段一致）
 * @return 逐通道生效时序
 * @throws std::runtime_error 任一通道的作用域含非法字段或取值
 */
inline ChannelTimings BuildGatewayChannelTimings(const AdapterExtend& extend, int slave) {
    ChannelTimings timings;
    for (unsigned channel = 0; channel < kGatewayChannelCount; ++channel) {
        ApplyGatewayCanTiming(extend.Scope(slave, static_cast<int>(channel)), &timings[channel]);
    }
    return timings;
}

/**
 * @brief 按从站实际通道数生成下发计划
 * @param extend 已解析的扩展配置信封
 * @param slave 从站号（与 `buses` 键的 slave 段一致）
 * @param channel_count 已识别的实际通道数；0 表示不支持的从站
 * @return 逐通道下发计划；未配置的通道为 `std::nullopt`
 * @throws std::runtime_error 任一通道的作用域含非法字段或取值
 */
inline ChannelTimingPlan BuildChannelTimingPlan(const AdapterExtend& extend, int slave,
                                                std::size_t channel_count) {
    if (channel_count > kGatewayChannelCount || (channel_count == 0 && !extend.Common().empty())) {
        throw std::runtime_error("Ethercat slave " + std::to_string(slave) +
                                 " has unsupported CAN channel count for timing configuration");
    }
    for (const auto& bus : extend.DeclaredBuses()) {
        if (bus.first == slave && static_cast<std::size_t>(bus.second) >= channel_count) {
            throw std::runtime_error("Ethercat slave " + std::to_string(slave) +
                                     " has no CAN channel '" + std::to_string(bus.second) + "'");
        }
    }
    ChannelTimingPlan plan(channel_count);
    for (std::size_t channel = 0; channel < channel_count; ++channel) {
        const auto scope = extend.Scope(slave, static_cast<int>(channel));
        if (!HasGatewayCanTiming(scope)) {
            continue;
        }
        CanTiming timing;
        ApplyGatewayCanTiming(scope, &timing);
        plan[channel] = timing;
    }
    return plan;
}

}  // namespace encos::ethercat
