#include "usb_serial_sender.h"

#include <algorithm>
#include <utility>

namespace encos {

UsbSerialSender::UsbSerialSender(Writer writer, LoggerPtr logger, std::size_t max_pending_retries,
                                 std::chrono::steady_clock::duration retry_delay)
    : writer_(std::move(writer)),
      logger_(std::move(logger)),
      max_pending_retries_(max_pending_retries),
      retry_delay_(retry_delay),
      worker_(&UsbSerialSender::Loop, this) {}

UsbSerialSender::~UsbSerialSender() {
    Stop();
}

void UsbSerialSender::Send(Frame frame) {
    {
        platform::LockGuard<platform::Mutex> lock(mutex_);
        if (!accepting_) {
            return;
        }
    }

    const int result = writer_(frame);
    if (result != 1 && logger_) {
        logger_->warn("Failed to write message to serial port: error code {}", result);
    }

    RetryTask task{std::chrono::steady_clock::now() + retry_delay_, std::move(frame)};
    {
        platform::LockGuard<platform::Mutex> lock(mutex_);
        if (!accepting_) {
            return;
        }
        if (retries_.size() >= max_pending_retries_) {
            ++dropped_retries_;
            if (logger_ && dropped_retries_ % 1024 == 1) {
                logger_->warn("USB serial retry queue is full; dropped {} retries",
                              dropped_retries_);
            }
            return;
        }
        const auto position = std::upper_bound(retries_.begin(), retries_.end(), task.deadline,
                                               [](const auto& deadline, const RetryTask& queued) {
                                                   return deadline < queued.deadline;
                                               });
        retries_.insert(position, std::move(task));
    }
    condition_.notify_one();
}

void UsbSerialSender::Stop() {
    {
        platform::LockGuard<platform::Mutex> lock(mutex_);
        accepting_ = false;
        retries_.clear();
    }
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void UsbSerialSender::Loop() {
    for (;;) {
        Frame frame;
        {
            platform::UniqueLock<platform::Mutex> lock(mutex_);
            condition_.wait(lock, [this]() {
                return !accepting_ || !retries_.empty();
            });
            if (!accepting_) {
                return;
            }

            const auto deadline = retries_.front().deadline;
            condition_.wait_until(lock, deadline);
            if (!accepting_) {
                return;
            }
            if (retries_.empty() || std::chrono::steady_clock::now() < retries_.front().deadline) {
                continue;
            }
            frame = std::move(retries_.front().frame);
            retries_.erase(retries_.begin());
        }

        const int result = writer_(frame);
        if (result != 1 && logger_) {
            logger_->warn("Retry failed to write message to serial port: error code {}", result);
        }
    }
}

}  // namespace encos
