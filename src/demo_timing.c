#include "example_soem/demo_timing.h"

#include <errno.h>

enum {
    kNanosecondsPerSecond = 1000000000L,
    kNanosecondsPerMicrosecond = 1000L,
};

/**
 * @brief 将 timespec 时间点增加指定的微秒数
 * @param[in,out] time_point 时间指针，会被原地修改
 * @param[in] period_us 要增加的微秒数
 */
void demo_timespec_add_us(struct timespec* time_point, long period_us) {
    if (time_point == 0 || period_us <= 0) {
        return;
    }

    time_point->tv_nsec += period_us * kNanosecondsPerMicrosecond;
    while (time_point->tv_nsec >= kNanosecondsPerSecond) {
        time_point->tv_nsec -= kNanosecondsPerSecond;
        ++time_point->tv_sec;
    }
}

/**
 * @brief 比较两个 timespec
 * @param[in] lhs 左侧时间
 * @param[in] rhs 右侧时间
 * @return lhs < rhs 返回 -1，lhs == rhs 返回 0，lhs > rhs 返回 1
 */
static int timespec_compare(const struct timespec* lhs, const struct timespec* rhs) {
    if (lhs->tv_sec < rhs->tv_sec) {
        return -1;
    }
    if (lhs->tv_sec > rhs->tv_sec) {
        return 1;
    }
    if (lhs->tv_nsec < rhs->tv_nsec) {
        return -1;
    }
    if (lhs->tv_nsec > rhs->tv_nsec) {
        return 1;
    }
    return 0;
}

/**
 * @brief 计算两个 timespec 的差值
 * @param[in] end 结束时间
 * @param[in] begin 开始时间
 * @return 时间差（end - begin）
 */
static struct timespec timespec_subtract(const struct timespec* end, const struct timespec* begin) {
    struct timespec result = {
        .tv_sec = end->tv_sec - begin->tv_sec,
        .tv_nsec = end->tv_nsec - begin->tv_nsec,
    };
    if (result.tv_nsec < 0) {
        result.tv_nsec += kNanosecondsPerSecond;
        --result.tv_sec;
    }
    return result;
}

/**
 * @brief 等待直到下一个周期
 * @param[in,out] next_cycle 下一个周期的时间点，会被更新为下一个周期
 * @param[in] period_us 周期时长（微秒）
 */
void demo_wait_until_next_cycle(struct timespec* next_cycle, long period_us) {
    if (next_cycle == 0 || period_us <= 0) {
        return;
    }

    demo_timespec_add_us(next_cycle, period_us);

    while (1) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (timespec_compare(&now, next_cycle) >= 0) {
            return;
        }

        struct timespec remaining = timespec_subtract(next_cycle, &now);
        while (nanosleep(&remaining, &remaining) != 0) {
            if (errno != EINTR) {
                return;
            }
        }
    }
}
