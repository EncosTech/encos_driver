#pragma once

#include "encos/export.h"

namespace encos::utils {

/**
 * @brief 将当前线程设置为指定的 Linux SCHED_FIFO 优先级，并锁定当前进程内存页
 * @param priority SCHED_FIFO 优先级，必须位于系统支持范围内
 * @return 优先级设置与内存锁页均成功时返回 true；任一失败或平台不支持时返回 false。
 *
 * 本函数把线程优先级提升与进程内存锁页合并为一次调用：
 * - 动态模式（默认）下通过 ThreadPriorityHelper 可执行文件提升目标线程优先级。
 *   ThreadPriorityHelper 需要 CAP_SYS_NICE；调用本函数的进程随后自己执行
 *   mlockall()，因此调用方还需要 CAP_IPC_LOCK（或 unlimited 的 RLIMIT_MEMLOCK）。
 * - 静态模式（ENCOS_STATIC_MODE）下本进程直接调用 sched_setscheduler() 提升当前
 *   线程优先级，因此调用方需要 CAP_SYS_NICE；随后同样自己执行 mlockall()，需要
 *   CAP_IPC_LOCK（或 unlimited 的 RLIMIT_MEMLOCK）。
 */
ENCOS_BASE_API bool SetCurrentThreadPriority(int priority);

}  // namespace encos::utils
