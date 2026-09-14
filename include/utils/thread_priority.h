#pragma once

#include "encos/export.h"

namespace encos::utils {

/**
 * @brief 设置当前线程优先级；Linux同时锁定当前进程内存页
 * @param priority
 * Linux为SCHED_FIFO优先级；Windows接受1–99，1–49映射为ABOVE_NORMAL，50–99映射为HIGHEST
 * @return Windows优先级设置成功，或Linux优先级设置与锁页均成功时返回true；失败或不支持返回false。
 *
 * Windows仅设置线程调度优先级，不修改进程优先级或锁定内存，也不保证实时调度。
 * Linux把线程优先级提升与进程内存锁页合并为一次调用：
 * - 动态模式（默认）下通过 ThreadPriorityHelper 可执行文件提升目标线程优先级。
 *   ThreadPriorityHelper 需要 CAP_SYS_NICE；调用本函数的进程随后自己执行
 *   mlockall()，因此调用方还需要 CAP_IPC_LOCK（或 unlimited 的 RLIMIT_MEMLOCK）。
 * - 静态模式（ENCOS_STATIC_MODE）下本进程直接调用 sched_setscheduler() 提升当前
 *   线程优先级，因此调用方需要 CAP_SYS_NICE；随后同样自己执行 mlockall()，需要
 *   CAP_IPC_LOCK（或 unlimited 的 RLIMIT_MEMLOCK）。
 */
ENCOS_BASE_API bool SetCurrentThreadPriority(int priority);

}  // namespace encos::utils
