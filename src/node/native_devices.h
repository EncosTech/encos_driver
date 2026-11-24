// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
#pragma once

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "node/native_api.h"

namespace encos::node {
/** @brief 检查 JS 数值的整数性与目标类型范围，防止截断或回绕。 */
template <typename T>
T CheckedInteger(const Json& value) {
    static_assert(std::is_integral_v<T>);
    if (!value.is_number()) {
        throw std::invalid_argument("expected an integer");
    }
    const long double number = value.get<long double>();
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < static_cast<long double>(std::numeric_limits<T>::lowest()) ||
        number >= std::ldexp(1.0L, std::numeric_limits<T>::digits)) {
        throw std::invalid_argument("integer is outside the supported range");
    }
    return static_cast<T>(number);
}
/** @brief 将电机状态转换为保留非有限数值的 JS 数据。 */
Json MotorStatusJson(const MotorStatus& status);
/** @brief 将电机属性转换为 JS 数据。 */
Json MotorPropertiesJson(const MotorProperties& properties);
/** @brief 从 JS 数据解析电机属性。 */
MotorProperties ParseMotorProperties(const Json& value);
}  // namespace encos::node
