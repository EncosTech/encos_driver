#include "serial_port.h"

#include <array>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "serial_handle.h"
#include "utils/thread_priority.h"

namespace encos {

struct SerialPort::Impl {
    explicit Impl(const std::string& path) : handle(std::make_unique<SerialHandle>(path)) {}
    std::unique_ptr<SerialHandle> handle;
    std::thread receiver;
    std::mutex write_mutex;
    std::atomic<bool> open{true};
    bool started = false;
};

SerialPort::SerialPort(const std::string& path) : impl_(std::make_unique<Impl>(path)) {}

SerialPort::~SerialPort() {
    Close();
}

void SerialPort::Start(ReceiveCallback receive, EventCallback disconnected,
                       EventCallback priority_failed) {
    if (!receive || impl_->started || !Ok()) {
        throw std::logic_error("Serial receive requires an open, unstarted port and callback");
    }
    impl_->started = true;
    impl_->receiver =
        std::thread([this, receive = std::move(receive), disconnected = std::move(disconnected),
                     priority_failed = std::move(priority_failed)]() {
            try {
                if (!utils::SetCurrentThreadPriority(50) && priority_failed) {
                    priority_failed();
                }
                std::array<std::byte, 4096> buffer{};
                while (Ok()) {
                    const int size = impl_->handle->Read(buffer.data(), buffer.size());
                    if (!Ok()) {
                        return;
                    }
                    if (size <= 0) {
                        break;
                    }
                    receive(buffer.data(), static_cast<std::size_t>(size));
                }
            } catch (...) {
                // 用户回调异常不能越过线程入口。
            }
            if (impl_->open.exchange(false) && disconnected) {
                try {
                    disconnected();
                } catch (...) {}
            }
        });
}

bool SerialPort::Write(const void* buffer, std::size_t size) {
    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    return Ok() && impl_->handle->Write(buffer, size);
}

bool SerialPort::Ok() const {
    return impl_->open.load();
}

void SerialPort::Close() {
    impl_->open.store(false);
    if (impl_->handle) {
        impl_->handle->Cancel();
    }
    if (impl_->receiver.joinable()) {
        impl_->receiver.join();
    }
    // 等待已经开始的写入退出后再释放句柄。
    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    impl_->handle.reset();
}

}  // namespace encos
