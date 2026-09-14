#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <utility>

namespace encos::ethernet {
/** @brief 固件周期上报心跳；重复数据报不刷新在线时间。 */
class ManagementHeartbeat {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto kTimeout = std::chrono::milliseconds(500);
    explicit ManagementHeartbeat(Clock::time_point now) : last_report_(now) {}

    bool Report(uint32_t sequence, uint32_t uptime, Clock::time_point now) {
        const auto key = std::make_pair(sequence, uptime);
        for (const auto& seen : recent_)
            if (seen == key)
                return false;
        recent_.push_back(key);
        if (recent_.size() > 64)
            recent_.pop_front();
        last_report_ = now;
        online_ = true;
        return true;
    }

    bool CheckTimeout(Clock::time_point now) {
        if (online_ && now - last_report_ >= kTimeout) {
            online_ = false;
            return true;
        }
        return false;
    }
    bool Online() const {
        return online_;
    }

private:
    Clock::time_point last_report_;
    bool online_ = true;
    std::deque<std::pair<uint32_t, uint32_t>> recent_;
};
}  // namespace encos::ethernet
