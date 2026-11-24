// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <optional>

#include "adapter/adapter_statistics.h"
#include "protocol/management_protocol.h"

namespace encos::ethernet {

/**
 * @brief 每总线统计新鲜度过滤，不改变管理线程的在线检测。
 * 序号及运行时间按32位串行数比较；任一串行量回退必须先观察到递增候选流，且旧流静默500ms。
 * 协议无启动标识，无法完美区分重启与静默后延迟到达的旧流；超过半范围的间隔也有歧义。
 */
class CanStatisticsOrder {
public:
    using Clock = std::chrono::steady_clock;

    enum class Result { Rejected, Accepted, Restarted };

    Result Observe(const CanError& event, Clock::time_point now) {
        const Sample sample{event.sequence, event.uptime, now};

        if (!last_) {
            last_ = sample;
            return Result::Accepted;
        }

        if (Progresses(*last_, sample)) {
            last_ = sample;
            candidate_.reset();
            return Result::Accepted;
        }

        const auto sequence_delta = sample.sequence - last_->sequence;
        const auto uptime_delta = sample.uptime - last_->uptime;
        if (sequence_delta == 0 || (sequence_delta < kHalfRange && uptime_delta < kHalfRange))
            return Result::Rejected;

        if (!candidate_) {
            candidate_ = sample;
            return Result::Rejected;
        }

        if (!Progresses(*candidate_, sample)) {
            if (now - candidate_->time >= std::chrono::milliseconds(500))
                candidate_ = sample;
            return Result::Rejected;
        }

        candidate_ = sample;

        if (now - last_->time >= std::chrono::milliseconds(500)) {
            last_ = sample;
            candidate_.reset();
            return Result::Restarted;
        }

        return Result::Rejected;
    }

private:
    struct Sample {
        std::uint32_t sequence;
        std::uint32_t uptime;
        Clock::time_point time;
    };

    static constexpr std::uint32_t kHalfRange = 0x80000000U;

    static bool Progresses(const Sample& previous, const Sample& next) {
        const auto sequence_delta = next.sequence - previous.sequence;
        const auto uptime_delta = next.uptime - previous.uptime;
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(next.time - previous.time)
                .count();

        return sequence_delta != 0 && sequence_delta < kHalfRange && uptime_delta < kHalfRange &&
               static_cast<std::uint64_t>(uptime_delta) <=
                   static_cast<std::uint64_t>(elapsed) + 500;
    }

    std::optional<Sample> last_, candidate_;
};

/** @brief 仅错误事件提供 ECR 计数，空闲心跳保留最后事件样本。 */
inline void ApplyCanStatistics(BusStatistics& bus, const CanError& event) {
    if (event.flags != 0) {
        bus.error_counters = CanErrorCounters{event.ecr & 0xffU, (event.ecr >> 8U) & 0x7fU,
                                              ErrorCounterKind::LastErrorEvent};
    }
}

}  // namespace encos::ethernet
