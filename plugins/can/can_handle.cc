// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "can_handle.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <system_error>

#include "platform/log.h"

namespace encos {
namespace {
bool enable_can_fd_frames(int fd) {
    int enable = 1;
    return setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable, sizeof(enable)) == 0;
}

LoggerPtr CanHandleLogger() {
    static LoggerPtr logger = CreateLogger("CanHandle", LogLevel::Error);
    return logger;
}

MotorPackMsg DecodeCanFrame(const struct canfd_frame& frame, ssize_t nbytes) {
    MotorPackMsg msg{};
    msg.id = (frame.can_id & CAN_EFF_FLAG) ? (frame.can_id & CAN_EFF_MASK)
                                           : (frame.can_id & CAN_SFF_MASK);
    uint8_t flags = 0;
    if ((frame.can_id & CAN_EFF_FLAG) != 0) {
        flags |= kCanFrameFlagEff;
    }
    if ((frame.can_id & CAN_RTR_FLAG) != 0) {
        flags |= kCanFrameFlagRtr;
    }
    if (nbytes == CANFD_MTU) {
        flags |= kCanFrameFlagFdMask;
    }
    msg.frame_flags = SanitizeCanFrameFlags(flags);
    msg.len = static_cast<uint8_t>(std::min<std::size_t>(frame.len, sizeof(msg.data)));
    if ((msg.frame_flags & kCanFrameFlagRtr) == 0) {
        std::memcpy(msg.data, frame.data, msg.len);
    }
    return msg;
}
}  // namespace

CanHandle::CanHandle(const std::string& interface_name) {
    can_fd_ = socket(PF_CAN, SOCK_RAW | SOCK_CLOEXEC, CAN_RAW);
    if (can_fd_ < 0) {
        throw std::runtime_error("Failed to Create CAN socket");
    }
    struct ifreq ifr {};
    std::strncpy(ifr.ifr_name, interface_name.c_str(), IFNAMSIZ - 1);
    if (ioctl(can_fd_, SIOCGIFINDEX, &ifr) < 0) {
        close(can_fd_);
        throw std::runtime_error("Failed to get interface index");
    }
    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(can_fd_, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        close(can_fd_);
        throw std::runtime_error("Failed to bind CAN socket");
    }
    fd_frames_enabled_ = enable_can_fd_frames(can_fd_);
    InitializeWakeFd();
    running_.store(true);
}

CanHandle::CanHandle(int existing_fd) {
    if (existing_fd < 0) {
        throw std::runtime_error("Invalid CAN socket fd");
    }
    can_fd_ = existing_fd;
    fd_frames_enabled_ = enable_can_fd_frames(can_fd_);
    InitializeWakeFd();
    running_.store(true);
}

void CanHandle::InitializeWakeFd() {
    wake_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wake_fd_ < 0) {
        const int error = errno;
        close(can_fd_);
        can_fd_ = -1;
        throw std::system_error(error, std::generic_category(), "create CAN wake event");
    }
}

CanHandle::~CanHandle() {
    Stop();
    close(wake_fd_);
    if (can_fd_ >= 0) {
        close(can_fd_);
        can_fd_ = -1;
    }
}

void CanHandle::Send(const MotorMessage& message) {
    if (!Ok()) {
        return;
    }
    const MotorPackMsg& msg = message.data;
    const uint8_t flags = SanitizeCanFrameFlags(msg.frame_flags);
    auto logger = CanHandleLogger();
    if (!CanFrameFlagsHaveValidCanFdBits(flags)) {
        logger->error("Invalid CAN FD flag bits in SocketCAN message: {:#04x}", flags);
        return;
    }
    if (msg.len > 8) {
        logger->error("SocketCAN message length exceeds supported payload: {}", msg.len);
        return;
    }

    const bool extended = CanFrameFlagsUseExtendedId(flags);
    const bool rtr = CanFrameFlagsUseRtr(flags);
    const bool can_fd = CanFrameFlagsUseCanFd(flags);
    const uint32_t max_id = extended ? CAN_EFF_MASK : CAN_SFF_MASK;
    if (msg.id > max_id) {
        logger->error("CAN id {:#x} exceeds {} frame range", msg.id,
                      extended ? "extended" : "standard");
        return;
    }

    canid_t can_id = msg.id;
    if (extended) {
        can_id |= CAN_EFF_FLAG;
    }

    int res = -1;
    if (can_fd) {
        if (rtr) {
            logger->error("CAN FD does not support RTR frames; dropping message id={:#x}", msg.id);
            return;
        }
        if (!fd_frames_enabled_) {
            logger->error(
                "SocketCAN FD frames are not enabled on this socket; dropping message id={:#x}",
                msg.id);
            return;
        }
        struct canfd_frame frame;
        std::memset(&frame, 0, sizeof(frame));
        frame.can_id = can_id;
        frame.len = msg.len;
        frame.flags = CANFD_BRS;
        std::memcpy(frame.data, msg.data, msg.len);
        res = send(can_fd_, &frame, CANFD_MTU, MSG_DONTWAIT | MSG_NOSIGNAL);
    } else {
        struct can_frame frame;
        std::memset(&frame, 0, sizeof(frame));
        frame.can_id = can_id;
        if (rtr) {
            frame.can_id |= CAN_RTR_FLAG;
        }
        frame.len = msg.len;
        if (!rtr) {
            std::memcpy(frame.data, msg.data, msg.len);
        }
        res = send(can_fd_, &frame, CAN_MTU, MSG_DONTWAIT | MSG_NOSIGNAL);
    }

    if (res < 0) {
        logger->error("Failed to write SocketCAN frame: {}", std::strerror(errno));
    }
}

void CanHandle::Loop() {
    constexpr std::size_t kBatchSize = 64;
    MotorMessages messages;
    messages.reserve(kBatchSize);
    while (running_.load()) {
        pollfd descriptors[] = {{can_fd_, POLLIN, 0}, {wake_fd_, POLLIN, 0}};
        const int ret = poll(descriptors, 2, -1);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            CanHandleLogger()->error("CAN poll failed: {}", std::strerror(errno));
            break;
        }
        if (descriptors[1].revents || !running_.load()) {
            break;
        }
        messages.clear();
        if (descriptors[0].revents & POLLIN) {
            for (std::size_t i = 0; i < kBatchSize && running_.load(); ++i) {
                struct canfd_frame frame {};
                const ssize_t nbytes =
                    recv(can_fd_, &frame, sizeof(frame), MSG_DONTWAIT | MSG_TRUNC);
                if (nbytes < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        CanHandleLogger()->error("CAN receive failed: {}", std::strerror(errno));
                        Stop();
                    }
                    break;
                }
                if (nbytes == 0) {
                    Stop();
                    break;
                }
                if ((nbytes != CAN_MTU && nbytes != CANFD_MTU) || (frame.can_id & CAN_ERR_FLAG) ||
                    frame.len > (nbytes == CAN_MTU ? CAN_MAX_DLEN : CANFD_MAX_DLEN)) {
                    continue;
                }
                MotorMessage message{};
                message.bus_idx = 0;
                message.data = DecodeCanFrame(frame, nbytes);
                messages.push_back(message);
            }
        }
        if (!messages.empty()) {
            if (batch_callback_) {
                batch_callback_(messages);
            } else if (callback_) {
                for (const auto& message : messages) {
                    callback_(message);
                }
            }
        }
        if (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            CanHandleLogger()->error("CAN socket is no longer operational");
            break;
        }
    }
    Stop();
}

void CanHandle::SetCallback(const std::function<void(MotorMessage)>& callback) {
    callback_ = callback;
    batch_callback_ = nullptr;
}

void CanHandle::SetBatchCallback(const std::function<void(const MotorMessages&)>& callback) {
    batch_callback_ = callback;
    callback_ = nullptr;
}

void CanHandle::Stop() {
    running_.store(false);
    const uint64_t signal = 1;
    while (write(wake_fd_, &signal, sizeof(signal)) < 0 && errno == EINTR) {}
}

bool CanHandle::Ok() {
    return can_fd_ >= 0 && running_.load();
}

}  // namespace encos
