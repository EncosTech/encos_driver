#pragma once

#include <type_traits>
#include <utility>

namespace encos::utils {

/**
 * @brief 在当前作用域退出时执行一次清理函数
 *
 * 该工具不分配内存，也不使用类型擦除。清理函数必须不抛异常。
 *
 * @tparam Function 清理函数类型
 */
template <typename Function>
class ScopeExit {
public:
    explicit ScopeExit(Function function) noexcept(std::is_nothrow_move_constructible_v<Function>)
        : function_(std::move(function)) {}

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ScopeExit(ScopeExit&&) = delete;
    ScopeExit& operator=(ScopeExit&&) = delete;

    ~ScopeExit() noexcept {
        function_();
    }

private:
    Function function_;
};

/**
 * @brief 创建由模板参数推导清理函数类型的 ScopeExit
 * @param function 作用域退出时执行的清理函数
 * @return 不分配内存的 ScopeExit 对象
 */
template <typename Function>
auto MakeScopeExit(Function&& function) {
    return ScopeExit<std::decay_t<Function>>(std::forward<Function>(function));
}

}  // namespace encos::utils
