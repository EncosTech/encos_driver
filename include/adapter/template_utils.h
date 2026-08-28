#pragma once

#include <type_traits>
#include <vector>

namespace encos {

/**
 * @brief 类型特征：判断 T 是否为 std::vector 类型
 * @tparam T 待检测的类型
 */
template <typename T>
struct is_std_vector : std::false_type {};

/**
 * @brief 类型特征特化：std::vector<U, Alloc> 匹配为 true
 */
template <typename U, typename Alloc>
struct is_std_vector<std::vector<U, Alloc>> : std::true_type {};

}  // namespace encos
