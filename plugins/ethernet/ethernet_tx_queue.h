#pragma once
#include <deque>
#include <utility>

#include "motor/types.h"
namespace encos::ethernet {
/** @brief 有界发送队列；按提交顺序保留同步批次边界。调用者负责加锁。 */
class TransmitQueue {
public:
    /** @brief 整批入队；容量不足或通道非法时不改变队列。 */
    bool Push(MotorMessages messages) {
        if (messages.size() > 4096 - size_)
            return false;
        for (const auto& message : messages)
            if (message.bus_idx < 0 || message.bus_idx >= 8)
                return false;
        if (messages.empty())
            return true;
        size_ += messages.size();
        batches_.push_back(std::move(messages));
        return true;
    }
    /** @brief 取出最早提交的完整批次。 */
    MotorMessages Pop() {
        if (batches_.empty())
            return {};
        auto batch = std::move(batches_.front());
        batches_.pop_front();
        size_ -= batch.size();
        return batch;
    }
    /** @brief 查询剩余容量；调用者负责加锁。 */
    std::size_t Available() const {
        return 4096 - size_;
    }
    /** @brief 查询是否为空。 */
    bool Empty() const {
        return size_ == 0;
    }

private:
    std::deque<MotorMessages> batches_;
    std::size_t size_ = 0;
};
}  // namespace encos::ethernet
