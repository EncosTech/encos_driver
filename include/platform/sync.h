#pragma once

#include <cerrno>
#include <exception>
#include <mutex>
#include <system_error>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <pthread.h>
#endif

/**
 * @brief 平台相关同步原语
 *
 * 提供跨平台的互斥锁与锁包装器。Linux 原生实现使用支持优先级继承的 pthread 互斥锁；
 * Emscripten 与其它平台退化为无操作或标准库互斥锁。
 */
namespace encos::platform {

#ifdef __EMSCRIPTEN__

/** @brief Emscripten 平台互斥锁不支持优先级继承 */
inline constexpr bool kMutexUsesPriorityInheritance = false;

/** @brief Emscripten 单线程无操作互斥锁 */
class Mutex {
public:
    /** @brief 获取锁（无操作） */
    void lock() {}
    /** @brief 释放锁（无操作） */
    void unlock() {}
    /**
     * @brief 尝试获取锁
     * @return 始终返回 true
     */
    bool try_lock() {
        return true;
    }
};

/** @brief Emscripten 无操作递归互斥锁 */
using RecursiveMutex = Mutex;

#elif defined(__linux__)

/** @brief Linux 原生互斥锁支持优先级继承 */
inline constexpr bool kMutexUsesPriorityInheritance = true;

namespace detail {

/**
 * @brief 将 pthread 错误码转换为 system_error 抛出
 * @param error pthread 返回的错误码
 * @param operation 发生错误的操作名
 */
inline void ThrowPthreadError(int error, const char* operation) {
    throw std::system_error(error, std::generic_category(), operation);
}

/**
 * @brief 基于 pthread 的互斥锁模板
 * @tparam Recursive 为 true 时使用递归互斥锁，否则使用普通互斥锁
 *
 * 初始化时设置 PTHREAD_PRIO_INHERIT 协议，保证实时优先级场景下的优先级继承语义。
 */
template <bool Recursive>
class PthreadMutex {
public:
    /** @brief 原生句柄类型 */
    using native_handle_type = pthread_mutex_t*;

    /** @brief 初始化 pthread 互斥锁并设置优先级继承协议 */
    PthreadMutex() {
        pthread_mutexattr_t attributes;
        int result = pthread_mutexattr_init(&attributes);
        if (result != 0) {
            ThrowPthreadError(result, "pthread_mutexattr_init");
        }
        try {
            result = pthread_mutexattr_setprotocol(&attributes, PTHREAD_PRIO_INHERIT);
            if (result != 0) {
                ThrowPthreadError(result, "pthread_mutexattr_setprotocol");
            }
            int protocol = PTHREAD_PRIO_NONE;
            result = pthread_mutexattr_getprotocol(&attributes, &protocol);
            if (result != 0) {
                ThrowPthreadError(result, "pthread_mutexattr_getprotocol");
            }
            if (protocol != PTHREAD_PRIO_INHERIT) {
                ThrowPthreadError(ENOTSUP, "pthread mutex priority inheritance");
            }
            uses_priority_inheritance_ = true;
            result = pthread_mutexattr_settype(
                &attributes, Recursive ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_NORMAL);
            if (result != 0) {
                ThrowPthreadError(result, "pthread_mutexattr_settype");
            }
            result = pthread_mutex_init(&mutex_, &attributes);
            if (result != 0) {
                ThrowPthreadError(result, "pthread_mutex_init");
            }
        } catch (...) {
            (void) pthread_mutexattr_destroy(&attributes);
            throw;
        }
        result = pthread_mutexattr_destroy(&attributes);
        if (result != 0) {
            (void) pthread_mutex_destroy(&mutex_);
            ThrowPthreadError(result, "pthread_mutexattr_destroy");
        }
    }

    PthreadMutex(const PthreadMutex&) = delete;
    PthreadMutex& operator=(const PthreadMutex&) = delete;

    /** @brief 销毁 pthread 互斥锁；销毁失败时终止进程 */
    ~PthreadMutex() {
        if (pthread_mutex_destroy(&mutex_) != 0) {
            std::terminate();
        }
    }

    /** @brief 获取锁；失败时抛出 system_error */
    void lock() {
        const int result = pthread_mutex_lock(&mutex_);
        if (result != 0) {
            ThrowPthreadError(result, "pthread_mutex_lock");
        }
    }

    /**
     * @brief 尝试获取锁
     * @return 成功获取时返回 true，锁被占用时返回 false
     */
    bool try_lock() {
        const int result = pthread_mutex_trylock(&mutex_);
        if (result == 0) {
            return true;
        }
        if (result == EBUSY) {
            return false;
        }
        ThrowPthreadError(result, "pthread_mutex_trylock");
        return false;
    }

    /** @brief 释放锁；失败时终止进程 */
    void unlock() noexcept {
        if (pthread_mutex_unlock(&mutex_) != 0) {
            std::terminate();
        }
    }

    /**
     * @brief 返回原生 pthread 互斥锁句柄
     * @return pthread 互斥锁指针
     */
    native_handle_type native_handle() noexcept {
        return &mutex_;
    }

    /**
     * @brief 查询构造时由 pthread 属性接口确认的优先级继承状态
     * @return 运行时属性为 PTHREAD_PRIO_INHERIT 时返回 true
     */
    bool UsesPriorityInheritance() const noexcept {
        return uses_priority_inheritance_;
    }

private:
    pthread_mutex_t mutex_{};
    bool uses_priority_inheritance_{false};
};

}  // namespace detail

/** @brief Linux 平台普通互斥锁 */
using Mutex = detail::PthreadMutex<false>;
/** @brief Linux 平台递归互斥锁 */
using RecursiveMutex = detail::PthreadMutex<true>;

#else

/** @brief 非 Linux/Emscripten 平台互斥锁不支持优先级继承 */
inline constexpr bool kMutexUsesPriorityInheritance = false;

/** @brief 标准库互斥锁 */
using Mutex = std::mutex;
/** @brief 标准库递归互斥锁 */
using RecursiveMutex = std::recursive_mutex;

#endif

/** @brief 锁守卫类型别名，与 std::lock_guard 语义一致 */
template <typename MutexType>
using LockGuard = std::lock_guard<MutexType>;

/** @brief 唯一锁类型别名，与 std::unique_lock 语义一致 */
template <typename MutexType>
using UniqueLock = std::unique_lock<MutexType>;

}  // namespace encos::platform
