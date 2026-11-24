// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <stdexcept>

#include "encos/export.h"

namespace encos::utils {

/** @brief 线程调度设置或 helper 授权失败。 */
class ENCOS_BASE_API ThreadPriorityError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/** @brief 调度成功后，发现进程锁页策略无效或系统调用失败。 */
class ENCOS_BASE_API MemoryLockError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief 设置当前线程优先级，并应用 Linux 进程锁页策略。
 * @param priority 调度优先级，含义同 SetCurrentThreadPriority。
 * @throws ThreadPriorityError 调度设置、helper 授权失败或平台不支持。
 * @throws MemoryLockError Linux 锁页策略无效或 `mlockall()` 失败。系统调用失败时，
 * 错误消息包含 `errno`、锁页限额和修复建议。
 */
ENCOS_BASE_API void RequireCurrentThreadPriority(int priority);

/**
 * @brief 设置当前线程优先级，Linux 默认同时锁定进程内存页。
 * @param priority Linux 使用 `SCHED_FIFO` 优先级。Windows 接受 1–99，
 * 其中 1–49 映射为 `ABOVE_NORMAL`，50–99 映射为 `HIGHEST`。
 * @return 线程优先级设置成功且所需锁页完成时返回 `true`，失败或平台不支持时返回 `false`。
 *
 * Windows 上的设置仅适用于当前线程的普通调度优先级。
 *
 * Linux 上，启动前设置 `ENCOS_MEMORY_LOCK=none` 可跳过进程锁页，适用于 V8 等需要回收
 * 页面内存的宿主。线程优先级照常设置，已锁定的内存保持锁定。未设置或设为 `all` 时执行锁页。
 * 其他值会导致调用失败。
 *
 * 使用默认锁页策略时，权限要求如下。
 * - 动态模式（默认）下通过 ThreadPriorityHelper 可执行文件提升目标线程优先级。
 *   ThreadPriorityHelper 需要 CAP_SYS_NICE；调用本函数的进程随后自己执行
 *   mlockall()，因此调用方还需要 CAP_IPC_LOCK（或 unlimited 的 RLIMIT_MEMLOCK）。
 * - 静态模式（ENCOS_STATIC_MODE）下本进程直接调用 sched_setscheduler() 提升当前
 *   线程优先级，因此调用方需要 CAP_SYS_NICE；随后同样自己执行 mlockall()，需要
 *   CAP_IPC_LOCK（或 unlimited 的 RLIMIT_MEMLOCK）。
 */
ENCOS_BASE_API bool SetCurrentThreadPriority(int priority);

}  // namespace encos::utils
