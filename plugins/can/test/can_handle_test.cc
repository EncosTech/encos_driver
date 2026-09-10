#include "can_handle.h"

#include <cerrno>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>

namespace {
class CanHandleTests : public testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets_), 0);
        handle_ = std::make_unique<encos::CanHandle>(sockets_[0]);
    }
    void TearDown() override {
        if (!handle_) {
            return;
        }
        handle_->Stop();
        if (thread_.joinable()) {
            thread_.join();
        }
        handle_.reset();
        close(sockets_[1]);
    }
    int sockets_[2]{};
    std::unique_ptr<encos::CanHandle> handle_;
    std::thread thread_;
};

TEST_F(CanHandleTests, StoppedHandleDoesNotSend) {
    handle_->Stop();
    encos::MotorMessage message{};
    message.data.id = 1;
    handle_->Send(message);
    pollfd descriptor{sockets_[1], POLLIN, 0};
    EXPECT_EQ(poll(&descriptor, 1, 20), 0);
}

TEST_F(CanHandleTests, PeerDisconnectStopsLoop) {
    std::promise<void> finished;
    auto future = finished.get_future();
    thread_ = std::thread([&] {
        handle_->Loop();
        finished.set_value();
    });
    shutdown(sockets_[1], SHUT_RDWR);
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(200)), std::future_status::ready);
    EXPECT_FALSE(handle_->Ok());
    handle_->Stop();
    thread_.join();
}

TEST_F(CanHandleTests, IdleLoopCanBeStopped) {
    std::promise<void> received;
    auto received_future = received.get_future();
    handle_->SetCallback([&](encos::MotorMessage) {
        received.set_value();
    });
    can_frame frame{};
    ASSERT_EQ(send(sockets_[1], &frame, sizeof(frame), 0), CAN_MTU);
    std::promise<void> finished;
    auto future = finished.get_future();
    thread_ = std::thread([&] {
        handle_->Loop();
        finished.set_value();
    });
    EXPECT_EQ(received_future.wait_for(std::chrono::milliseconds(200)), std::future_status::ready);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    handle_->Stop();
    const auto result = future.wait_for(std::chrono::milliseconds(200));
    EXPECT_EQ(result, std::future_status::ready);
    if (result != std::future_status::ready) {
        shutdown(sockets_[1], SHUT_RDWR);
    }
    thread_.join();
}

TEST_F(CanHandleTests, DrainsBacklogInBoundedBatchesPreservingFdAndExtendedId) {
    constexpr unsigned kCount = 130;
    for (unsigned i = 0; i < kCount; ++i) {
        canfd_frame frame{};
        frame.can_id = CAN_EFF_FLAG | (0x1000 + i);
        frame.len = 8;
        frame.data[0] = static_cast<uint8_t>(i);
        ASSERT_EQ(send(sockets_[1], &frame, sizeof(frame), MSG_DONTWAIT), CANFD_MTU);
    }
    std::promise<void> finished;
    auto future = finished.get_future();
    unsigned count = 0;
    unsigned batches = 0;
    handle_->SetBatchCallback([&](const encos::MotorMessages& messages) {
        EXPECT_LE(messages.size(), 64U);
        ++batches;
        for (const auto& message : messages) {
            EXPECT_EQ(message.data.id, 0x1000 + count);
            EXPECT_EQ(message.data.data[0], count);
            EXPECT_TRUE(encos::CanFrameFlagsUseCanFd(message.data.frame_flags));
            EXPECT_TRUE(encos::CanFrameFlagsUseExtendedId(message.data.frame_flags));
            ++count;
        }
        if (count == kCount) {
            handle_->Stop();
        }
    });
    thread_ = std::thread([&] {
        handle_->Loop();
        finished.set_value();
    });
    EXPECT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    handle_->Stop();
    thread_.join();
    EXPECT_EQ(count, kCount);
    EXPECT_EQ(batches, 3U);
}

TEST_F(CanHandleTests, IgnoresInvalidDatagramsAndErrorFrames) {
    canfd_frame invalid{};
    ASSERT_EQ(send(sockets_[1], &invalid, 1, 0), 1);
    invalid.can_id = CAN_ERR_FLAG;
    ASSERT_EQ(send(sockets_[1], &invalid, CAN_MTU, 0), CAN_MTU);
    invalid.can_id = 1;
    invalid.len = 9;
    ASSERT_EQ(send(sockets_[1], &invalid, CAN_MTU, 0), CAN_MTU);
    char oversized[CANFD_MTU + 1]{};
    ASSERT_EQ(send(sockets_[1], oversized, sizeof(oversized), 0), sizeof(oversized));
    can_frame valid{};
    valid.can_id = 7;
    valid.len = 1;
    valid.data[0] = 42;
    ASSERT_EQ(send(sockets_[1], &valid, sizeof(valid), 0), CAN_MTU);
    unsigned count = 0;
    handle_->SetCallback([&](encos::MotorMessage message) {
        ++count;
        EXPECT_EQ(message.data.id, 7U);
        EXPECT_EQ(message.data.data[0], 42);
        handle_->Stop();
    });
    auto task = std::async(std::launch::async, [&] {
        handle_->Loop();
    });
    EXPECT_EQ(task.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    handle_->Stop();
    task.get();
    EXPECT_EQ(count, 1U);
}

TEST_F(CanHandleTests, SendDoesNotWaitForBackpressure) {
    can_frame frame{};
    while (send(sockets_[0], &frame, sizeof(frame), MSG_DONTWAIT) >= 0) {}
    ASSERT_EQ(errno, EAGAIN);
    encos::MotorMessage message{};
    message.data.id = 1;
    auto task = std::async(std::launch::async, [&] {
        handle_->Send(message);
    });
    EXPECT_EQ(task.wait_for(std::chrono::milliseconds(200)), std::future_status::ready);
    shutdown(sockets_[1], SHUT_RDWR);
    task.get();
}

TEST_F(CanHandleTests, SendsIndependentFramesWithoutWaitingForReplies) {
    for (unsigned i = 0; i < 3; ++i) {
        encos::MotorMessage message{};
        message.data.id = i + 1;
        message.data.len = 1;
        message.data.data[0] = static_cast<uint8_t>(i);
        handle_->Send(message);
    }
    for (unsigned i = 0; i < 3; ++i) {
        can_frame frame{};
        ASSERT_EQ(recv(sockets_[1], &frame, sizeof(frame), MSG_DONTWAIT), CAN_MTU);
        EXPECT_EQ(frame.can_id, i + 1);
        EXPECT_EQ(frame.data[0], i);
    }
    pollfd descriptor{sockets_[1], POLLIN, 0};
    EXPECT_EQ(poll(&descriptor, 1, 20), 0);
}
}  // namespace
