#pragma once

#include <string>
#include <tuple>
#include <utility>

#include "encos/driver_manager.h"

namespace encos {

/**
 * @brief 测试中持有由管理器唯一拥有的适配器裸指针，并在作用域结束时删除它
 */
template <typename Adapter>
class ManagedAdapterGuard {
public:
    explicit ManagedAdapterGuard(Adapter* adapter) : adapter_(adapter) {}

    ~ManagedAdapterGuard() {
        if (adapter_ != nullptr) {
            (void) EncosDriverManager::Instance().DestroyAdapter(adapter_);
        }
    }

    ManagedAdapterGuard(const ManagedAdapterGuard&) = delete;
    ManagedAdapterGuard& operator=(const ManagedAdapterGuard&) = delete;

    ManagedAdapterGuard(ManagedAdapterGuard&& other) noexcept : adapter_(other.adapter_) {
        other.adapter_ = nullptr;
    }

    ManagedAdapterGuard& operator=(ManagedAdapterGuard&& other) noexcept {
        if (this != &other) {
            if (adapter_ != nullptr) {
                (void) EncosDriverManager::Instance().DestroyAdapter(adapter_);
            }
            adapter_ = other.adapter_;
            other.adapter_ = nullptr;
        }
        return *this;
    }

    Adapter* get() const noexcept {
        return adapter_;
    }

    Adapter* operator->() const noexcept {
        return adapter_;
    }

    Adapter& operator*() const noexcept {
        return *adapter_;
    }

private:
    Adapter* adapter_ = nullptr;
};

/**
 * @brief 通过管理器创建测试适配器
 */
template <typename Adapter, typename... Args>
ManagedAdapterGuard<Adapter> MakeManagedAdapter(const std::string& interface_name, Args&&... args) {
    auto arguments = std::make_tuple(std::forward<Args>(args)...);
    auto* adapter = static_cast<Adapter*>(EncosDriverManager::Instance().CreateAdapterWithFactory(
        interface_name, [interface_name, arguments = std::move(arguments)]() mutable {
            return std::apply(
                [&interface_name](auto&&... unpacked) {
                    return new Adapter(interface_name,
                                       std::forward<decltype(unpacked)>(unpacked)...);
                },
                std::move(arguments));
        }));
    return ManagedAdapterGuard<Adapter>(adapter);
}

}  // namespace encos
