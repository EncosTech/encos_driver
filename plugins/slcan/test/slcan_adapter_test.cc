#include "slcan_adapter.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>

#include "base_adapter_test_access.h"
#include "encos/driver_manager.h"

namespace encos {
namespace {

class TestSlcanAdapter : public SlcanAdapter {
public:
    explicit TestSlcanAdapter(const std::string& path)
        : SlcanAdapter(path, "SlcanPtyTest", LogLevel::Off) {}
    using SlcanAdapter::Send;
};

struct ManagedSlcanDeleter {
    void operator()(TestSlcanAdapter* adapter) const {
        EncosDriverManager::Instance().DestroyAdapter(adapter);
    }
};

class SlcanPtyTest : public ::testing::Test {
protected:
    void SetUp() override {
        master_ = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        ASSERT_GE(master_, 0);
        ASSERT_EQ(grantpt(master_), 0);
        ASSERT_EQ(unlockpt(master_), 0);
        const char* path = ptsname(master_);
        ASSERT_NE(path, nullptr);
        const std::string device_path = path;
        adapter_.reset(static_cast<TestSlcanAdapter*>(
            EncosDriverManager::Instance().CreateAdapterWithFactory(device_path, [device_path] {
                return new TestSlcanAdapter(device_path);
            })));
        EXPECT_EQ(ReadCommands(5), "S8\rO\r");
    }

    void TearDown() override {
        adapter_.reset();
        if (master_ >= 0) {
            close(master_);
        }
    }

    std::string ReadCommands(std::size_t count) const {
        std::string result;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (result.size() < count && std::chrono::steady_clock::now() < deadline) {
            pollfd descriptor{master_, POLLIN, 0};
            if (poll(&descriptor, 1, 20) <= 0) {
                continue;
            }
            std::array<char, 128> buffer{};
            const auto size = read(master_, buffer.data(), buffer.size());
            if (size > 0) {
                result.append(buffer.data(), static_cast<std::size_t>(size));
            }
        }
        return result;
    }

    int master_ = -1;
    std::unique_ptr<TestSlcanAdapter, ManagedSlcanDeleter> adapter_;
};

TEST_F(SlcanPtyTest, SerializesStandardExtendedAndRemoteFrames) {
    MotorMessage message{};
    message.data.id = 0x123;
    message.data.len = 2;
    message.data.data[0] = 0xab;
    message.data.data[1] = 0xcd;
    adapter_->Send(message);
    EXPECT_EQ(ReadCommands(10), "t1232abcd\r");

    message.data.id = 0x1234567;
    message.data.frame_flags = kCanFrameFlagEff;
    adapter_->Send(message);
    EXPECT_EQ(ReadCommands(15), "T012345672abcd\r");

    message.data.frame_flags = kCanFrameFlagEff | kCanFrameFlagRtr;
    adapter_->Send(message);
    EXPECT_EQ(ReadCommands(11), "R012345672\r");
}

TEST_F(SlcanPtyTest, ReceivesFragmentedAndCoalescedFrames) {
    std::mutex mutex;
    std::condition_variable received;
    MotorMessages messages;
    BaseAdapterTestAccess::SetRawMessageCallback(adapter_.get(), [&](const MotorMessages& batch) {
        std::lock_guard<std::mutex> lock(mutex);
        messages.insert(messages.end(), batch.begin(), batch.end());
        received.notify_all();
    });
    EXPECT_EQ(write(master_, "t1232ab", 7), 7);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const std::string remainder = "cd\rT012345671ef\rr3214\r";
    EXPECT_EQ(write(master_, remainder.data(), remainder.size()),
              static_cast<ssize_t>(remainder.size()));
    {
        std::unique_lock<std::mutex> lock(mutex);
        EXPECT_TRUE(received.wait_for(lock, std::chrono::seconds(1), [&] {
            return messages.size() >= 3;
        }));
    }
    adapter_.reset();
    ASSERT_EQ(messages.size(), 3U);
    EXPECT_EQ(messages[0].data.id, 0x123U);
    EXPECT_EQ(messages[0].data.data[1], 0xcd);
    EXPECT_EQ(messages[1].data.id, 0x1234567U);
    EXPECT_EQ(messages[1].data.frame_flags, kCanFrameFlagEff);
    EXPECT_EQ(messages[2].data.frame_flags, kCanFrameFlagRtr);
    EXPECT_EQ(messages[2].data.len, 4);
}

TEST_F(SlcanPtyTest, IdleShutdownWritesCloseAndWakesReceive) {
    const auto start = std::chrono::steady_clock::now();
    adapter_.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(500));
    EXPECT_EQ(ReadCommands(2), "C\r");
}

TEST_F(SlcanPtyTest, DisconnectMarksAdapterUnavailableAndStopsReceive) {
    close(master_);
    master_ = -1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (adapter_->Ok() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_FALSE(adapter_->Ok());
    const auto start = std::chrono::steady_clock::now();
    adapter_.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(500));
}

}  // namespace
}  // namespace encos
