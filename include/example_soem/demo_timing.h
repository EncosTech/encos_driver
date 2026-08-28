#pragma once

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将 timespec 时间点增加指定的微秒数
 * @param[in,out] time_point 时间指针，会被原地修改
 * @param[in] period_us 要增加的微秒数
 */
void demo_timespec_add_us(struct timespec* time_point, long period_us);

/**
 * @brief 等待直到下一个周期
 * @param[in,out] next_cycle 下一个周期的时间点，会被更新为下一个周期
 * @param[in] period_us 周期时长（微秒）
 */
void demo_wait_until_next_cycle(struct timespec* next_cycle, long period_us);

#ifdef __cplusplus
}
#endif
