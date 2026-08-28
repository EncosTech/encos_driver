#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "platform/log.h"
#include "platform/sync.h"

namespace encos {

class UsbSerialSender {
public:
    using Frame = std::vector<std::byte>;
    using Writer = std::function<int(const Frame&)>;

    UsbSerialSender(Writer writer, LoggerPtr logger, std::size_t max_pending_retries,
                    std::chrono::steady_clock::duration retry_delay);
    ~UsbSerialSender();

    UsbSerialSender(const UsbSerialSender&) = delete;
    UsbSerialSender& operator=(const UsbSerialSender&) = delete;

    void Send(Frame frame);
    void Stop();

private:
    struct RetryTask {
        std::chrono::steady_clock::time_point deadline;
        Frame frame;
    };

    void Loop();

    Writer writer_;
    LoggerPtr logger_;
    std::size_t max_pending_retries_;
    std::chrono::steady_clock::duration retry_delay_;
    platform::Mutex mutex_;
    std::condition_variable_any condition_;
    std::vector<RetryTask> retries_;
    bool accepting_ = true;
    std::size_t dropped_retries_ = 0;
    std::thread worker_;
};

}  // namespace encos
