#include <gtest/gtest.h>

#if defined(__linux__)
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <mutex>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "can_handle.h"

namespace fs = std::filesystem;

namespace {

bool RunQuietCommand(const std::string& cmd) {
    std::string full = cmd + " >/dev/null 2>&1";
    return std::system(full.c_str()) == 0;
}

bool EnsureVcanInterface(const std::string& ifname) {
    if (!RunQuietCommand("command -v ip")) {
        return false;
    }

    if (!RunQuietCommand("ip link show " + ifname)) {
        (void) RunQuietCommand("modprobe vcan");
        if (!RunQuietCommand("ip link add dev " + ifname + " type vcan")) {
            if (!RunQuietCommand("ip link show " + ifname)) {
                return false;
            }
        }
    }

    return RunQuietCommand("ip link set dev " + ifname + " up");
}

int OpenCanSocket(const std::string& ifname) {
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        return -1;
    }
    const int enable = 1;
    if (setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable, sizeof(enable)) < 0) {
        close(fd);
        return -1;
    }

    struct ifreq ifr {};
    std::snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname.c_str());
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        close(fd);
        return -1;
    }

    struct sockaddr_can addr {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

void CheckVcanReceive(bool can_fd) {
    const std::string ifname = "vcan0";
    if (!EnsureVcanInterface(ifname)) {
        GTEST_SKIP() << "vcan setup failed (need iproute2 and sufficient permissions).";
    }

    encos::CanHandle receiver(ifname);
    std::mutex msg_mutex;
    std::condition_variable msg_cv;
    bool got_frame = false;
    encos::MotorMessage received_msg{};
    receiver.SetCallback([&](encos::MotorMessage msg) {
        {
            std::lock_guard<std::mutex> lock(msg_mutex);
            received_msg = msg;
            got_frame = true;
        }
        msg_cv.notify_one();
    });
    const int tx_fd = OpenCanSocket(ifname);
    ASSERT_GE(tx_fd, 0);
    std::thread receiver_thread([&]() {
        receiver.Loop();
    });

    struct canfd_frame frame {};
    frame.can_id = 0x123;
    frame.len = 3;
    std::memset(frame.data, 0xA5, sizeof(frame.data));
    for (int i = 0; i < frame.len; ++i) {
        frame.data[i] = static_cast<uint8_t>(i + 1);
    }

    const auto frame_size = can_fd ? CANFD_MTU : CAN_MTU;
    EXPECT_EQ(write(tx_fd, &frame, frame_size), frame_size);

    std::unique_lock<std::mutex> lock(msg_mutex);
    const bool arrived = msg_cv.wait_for(lock, std::chrono::seconds(2), [&]() {
        return got_frame;
    });
    lock.unlock();

    close(tx_fd);
    receiver.Stop();
    if (receiver_thread.joinable()) {
        receiver_thread.join();
    }

    ASSERT_TRUE(arrived) << "No CAN frame received by CanHandle on vcan0";
    EXPECT_EQ(received_msg.data.id, frame.can_id);
    EXPECT_EQ(received_msg.data.len, frame.len);
    EXPECT_EQ(encos::CanFrameFlagsUseCanFd(received_msg.data.frame_flags), can_fd);
    EXPECT_EQ(std::memcmp(received_msg.data.data, frame.data, frame.len), 0);
    for (std::size_t i = frame.len; i < sizeof(received_msg.data.data); ++i) {
        EXPECT_EQ(received_msg.data.data[i], 0);
    }
}

}  // namespace

TEST(CanVirtualInterfaceTests, CanPluginReceiveOnVcan) {
    CheckVcanReceive(false);
}

TEST(CanVirtualInterfaceTests, CanPluginReceiveCanFdOnVcan) {
    CheckVcanReceive(true);
}

TEST(CanVirtualInterfaceTests, CanFdSendUsesConfiguredDataBitrate) {
    const std::string ifname = "vcan0";
    if (!EnsureVcanInterface(ifname)) {
        GTEST_SKIP() << "vcan setup failed (need iproute2 and sufficient permissions).";
    }
    const int rx_fd = OpenCanSocket(ifname);
    ASSERT_GE(rx_fd, 0);
    encos::CanHandle sender(ifname);
    encos::MotorMessage message{};
    message.data.id = 0x321;
    message.data.frame_flags = encos::kCanFrameFlagFdMask;
    message.data.len = 2;
    message.data.data[0] = 0x12;
    message.data.data[1] = 0x34;
    sender.Send(message);
    pollfd descriptor{rx_fd, POLLIN, 0};
    const int ready = poll(&descriptor, 1, 1000);
    canfd_frame frame{};
    const auto bytes = recv(rx_fd, &frame, sizeof(frame), MSG_DONTWAIT);
    close(rx_fd);
    ASSERT_EQ(ready, 1);
    ASSERT_EQ(bytes, CANFD_MTU);
    EXPECT_EQ(frame.can_id, message.data.id);
    EXPECT_EQ(frame.len, message.data.len);
    EXPECT_EQ(std::memcmp(frame.data, message.data.data, message.data.len), 0);
    EXPECT_NE(frame.flags & CANFD_BRS, 0);
}

#else

TEST(CanVirtualInterfaceTests, LinuxOnly) {
    GTEST_SKIP() << "Virtual CAN integration test is Linux-only.";
}

#endif
