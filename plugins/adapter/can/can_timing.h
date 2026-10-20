// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <limits>
#include <stdexcept>
#include <string>

#include "can_socket_setup.h"
#include "external/json.hpp"

namespace encos::can {

/**
 * @brief 本插件认识的位时序字段名
 *
 * 字段名与 Ethernet 插件统一（两者配置的都是 CAN 总线时序）。
 */
inline constexpr const char* kCanTimingFields[] = {"bitrate", "sample_point", "dbitrate",
                                                   "dsample_point"};

/**
 * @brief 判断字段名是否为已知的位时序字段
 * @param key 字段名
 * @return 是否为已知字段
 */
inline bool IsCanTimingField(const std::string& key) {
    for (const char* field : kCanTimingFields) {
        if (key == field) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 读取作用域中的位率字段，缺省时沿用默认值
 * @param scope 该总线的生效作用域
 * @param key 字段名
 * @param fallback 字段缺省时的取值
 * @return 生效值
 * @throws std::runtime_error 字段存在但不是正整数
 */
inline int ReadCanTimingRate(const nlohmann::json& scope, const char* key, int fallback) {
    if (!scope.contains(key)) {
        return fallback;
    }
    const auto& value = scope.at(key);
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        throw std::runtime_error(std::string("CAN extend '") + key + "' must be an integer");
    }
    const auto parsed = value.get<long long>();
    if (parsed <= 0 || parsed > std::numeric_limits<int>::max()) {
        throw std::runtime_error(std::string("CAN extend '") + key +
                                 "' must be a positive bitrate in bit/s");
    }
    return static_cast<int>(parsed);
}

/**
 * @brief 读取作用域中的采样点字段，缺省时沿用默认值
 * @param scope 该总线的生效作用域
 * @param key 字段名
 * @param fallback 字段缺省时的取值
 * @return 生效采样点，取值落在 (0, 1]
 * @throws std::runtime_error 字段存在但不是数值或超出取值范围
 */
inline double ReadCanTimingSamplePoint(const nlohmann::json& scope, const char* key,
                                       double fallback) {
    if (!scope.contains(key)) {
        return fallback;
    }
    const auto& value = scope.at(key);
    if (!value.is_number()) {
        throw std::runtime_error(std::string("CAN extend '") + key + "' must be a number");
    }
    const double parsed = value.get<double>();
    if (!(parsed > 0.0 && parsed <= 1.0)) {
        throw std::runtime_error(std::string("CAN extend '") + key +
                                 "' must be a sample point in (0, 1]");
    }
    return parsed;
}

/**
 * @brief 把某条总线的生效作用域覆盖到 CAN 位时序上
 * @param scope 由 `AdapterExtend::Scope(slave, bus)` 给出的生效作用域
 * @param timing 待覆盖的位时序；成员当前取值即默认值
 * @throws std::runtime_error 出现本插件不认识的字段，或字段类型/取值非法
 *
 * 默认值只在 `CanTiming` 的成员初始化处声明一次：作用域缺省该字段时保留传入值，因此
 * 空信封、只给公共参数与只给该总线部分字段走同一条路径。
 */
inline void ApplyCanTiming(const nlohmann::json& scope, CanTiming* timing) {
    if (timing == nullptr) {
        throw std::invalid_argument("ApplyCanTiming requires a non-null timing target");
    }
    for (const auto& [key, value] : scope.items()) {
        if (!IsCanTimingField(key)) {
            throw std::runtime_error("CAN extend field '" + key + "' is not supported");
        }
    }
    timing->bitrate = ReadCanTimingRate(scope, "bitrate", timing->bitrate);
    timing->sample_point = ReadCanTimingSamplePoint(scope, "sample_point", timing->sample_point);
    timing->dbitrate = ReadCanTimingRate(scope, "dbitrate", timing->dbitrate);
    timing->dsample_point = ReadCanTimingSamplePoint(scope, "dsample_point", timing->dsample_point);
}

}  // namespace encos::can
