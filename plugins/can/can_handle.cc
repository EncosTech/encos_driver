#include "can_handle.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdexcept>

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
    can_fd_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can_fd_ < 0) {
        throw std::runtime_error("Failed to Create CAN socket");
    }
    struct ifreq ifr;
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
    running_.store(true);
}

CanHandle::CanHandle(int existing_fd) {
    if (existing_fd < 0) {
        throw std::runtime_error("Invalid CAN socket fd");
    }
    can_fd_ = existing_fd;
    fd_frames_enabled_ = enable_can_fd_frames(can_fd_);
    running_.store(true);
}

CanHandle::~CanHandle() {
    Stop();
    if (can_fd_ >= 0) {
        close(can_fd_);
        can_fd_ = -1;
    }
}

void CanHandle::Send(const MotorMessage& message) {
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
        std::memcpy(frame.data, msg.data, msg.len);
        res = write(can_fd_, &frame, CANFD_MTU);
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
        res = write(can_fd_, &frame, CAN_MTU);
    }

    if (res < 0) {
        logger->error("Failed to write SocketCAN frame: {}", std::strerror(errno));
    }
}

void CanHandle::Loop() {
    struct pollfd pfd;
    pfd.fd = can_fd_;
    pfd.events = POLLIN;

    while (running_) {
        int ret = poll(&pfd, 1, 5);

        if (ret > 0) {
            if (pfd.revents & POLLIN) {
                struct canfd_frame frame;
                std::memset(&frame, 0, sizeof(frame));
                const ssize_t nbytes = ::read(can_fd_, &frame, sizeof(frame));
                if (nbytes == CAN_MTU || nbytes == CANFD_MTU) {
                    MotorPackMsg msg = DecodeCanFrame(frame, nbytes);

                    MotorMessage message;
                    message.bus_idx = 0;
                    message.data = msg;

                    if (callback_) {
                        callback_(message);
                    }
                }
            }
        } else if (ret < 0) {
            if (errno != EINTR) {
                // Error, but no Logger
            }
        }
    }
}

void CanHandle::SetCallback(const std::function<void(MotorMessage)>& callback) {
    callback_ = callback;
}

void CanHandle::Stop() {
    running_.store(false);
}

bool CanHandle::Ok() {
    return can_fd_ >= 0 && running_.load();
}

}  // namespace encos
