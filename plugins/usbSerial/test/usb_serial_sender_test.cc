#include "usb_serial_sender.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <gtest/gtest.h>
#include <vector>

namespace encos {
namespace {

TEST(UsbSerialSenderTests, FirstWritesAreNotLimitedByRetryQueueCapacity) {
    std::atomic<int> write_count{0};
    UsbSerialSender sender(
        [&write_count](const UsbSerialSender::Frame&) {
            return ++write_count > 0 ? 1 : 0;
        },
        CreateLogger("UsbSerialSenderCapacityTest", LogLevel::Off), 2, std::chrono::hours(1));

    for (int index = 0; index < 5; ++index) {
        sender.Send(UsbSerialSender::Frame{static_cast<std::byte>(index)});
    }

    EXPECT_EQ(write_count.load(), 5);
    sender.Stop();
}

TEST(UsbSerialSenderTests, StopCancelsPendingRetries) {
    std::atomic<int> write_count{0};
    UsbSerialSender sender(
        [&write_count](const UsbSerialSender::Frame&) {
            ++write_count;
            return 1;
        },
        CreateLogger("UsbSerialSenderStopTest", LogLevel::Off), 2, std::chrono::hours(1));
    sender.Send(UsbSerialSender::Frame{std::byte{0x01}});

    const auto start = std::chrono::steady_clock::now();
    sender.Stop();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(write_count.load(), 1);
    EXPECT_LT(elapsed, std::chrono::milliseconds(100));
}

}  // namespace
}  // namespace encos
