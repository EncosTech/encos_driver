// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <system_error>
#include <termios.h>
#include <unistd.h>

#include "serial_handle.h"

namespace encos {

struct SerialHandle::Impl {
    int fd_ = -1;
    int wake_read_ = -1;
    int wake_write_ = -1;
    ~Impl() {
        for (const int descriptor : {fd_, wake_read_, wake_write_}) {
            if (descriptor >= 0)
                close(descriptor);
        }
    }
};

SerialHandle::SerialHandle(const std::string& path) : impl_(std::make_unique<Impl>()) {
    try {
        impl_->fd_ = open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (impl_->fd_ < 0) {
            throw std::system_error(errno, std::generic_category(), "open serial port");
        }
        termios options{};
        if (tcgetattr(impl_->fd_, &options) != 0) {
            throw std::system_error(errno, std::generic_category(), "tcgetattr");
        }
        cfmakeraw(&options);
        options.c_cflag &= ~CSTOPB;
        options.c_iflag &= ~(IXON | IXOFF | IXANY);
        options.c_cflag |= CLOCAL | CREAD;
#ifdef CRTSCTS
        options.c_cflag &= ~CRTSCTS;
#endif
        options.c_cc[VMIN] = 1;
        options.c_cc[VTIME] = 0;
        if (cfsetispeed(&options, B115200) != 0 || cfsetospeed(&options, B115200) != 0 ||
            tcsetattr(impl_->fd_, TCSANOW, &options) != 0) {
            throw std::system_error(errno, std::generic_category(), "configure serial port");
        }
        int descriptors[2];
        if (pipe(descriptors) != 0) {
            throw std::system_error(errno, std::generic_category(), "create serial wake pipe");
        }
        impl_->wake_read_ = descriptors[0];
        impl_->wake_write_ = descriptors[1];
        for (const int descriptor : descriptors) {
            if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0 ||
                fcntl(descriptor, F_SETFL, O_NONBLOCK) < 0) {
                throw std::system_error(errno, std::generic_category(), "configure wake pipe");
            }
        }
    } catch (...) {
        impl_.reset();
        throw;
    }
}

SerialHandle::~SerialHandle() = default;

void SerialHandle::Cancel() noexcept {
    const char signal = 1;
    while (write(impl_->wake_write_, &signal, 1) < 0 && errno == EINTR) {}
}

int SerialHandle::Read(void* buffer, std::size_t capacity) {
    if (capacity == 0) {
        errno = EINVAL;
        return -1;
    }
    for (;;) {
        pollfd descriptors[] = {{impl_->fd_, POLLIN, 0}, {impl_->wake_read_, POLLIN, 0}};
        if (poll(descriptors, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (descriptors[1].revents != 0) {
            return 0;
        }
        if ((descriptors[0].revents & POLLIN) != 0) {
            const auto size = read(impl_->fd_, buffer, capacity);
            if (size > 0) {
                return static_cast<int>(size);
            }
            if (size < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            }
        }
        errno = EIO;
        return -1;
    }
}

bool SerialHandle::Write(const void* buffer, std::size_t size) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    const auto* bytes = static_cast<const std::byte*>(buffer);
    std::size_t offset = 0;
    while (offset < size) {
        const auto written = write(impl_->fd_, bytes + offset, size - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            if (std::chrono::steady_clock::now() < deadline) {
                continue;
            }
            return false;
        }
        if (written == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            return false;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            return false;
        }
        pollfd descriptors[] = {{impl_->fd_, POLLOUT, 0}, {impl_->wake_read_, POLLIN, 0}};
        const int result = poll(descriptors, 2, static_cast<int>(remaining.count()));
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0 || descriptors[1].revents != 0 ||
            (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return false;
        }
    }
    return true;
}

}  // namespace encos
