// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "usb_serial_adapter.h"

#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <future>
#include <gtest/gtest.h>
#include <poll.h>
#include <stdexcept>
#include <termios.h>
#include <thread>
#include <unistd.h>

#include "bus/bus.h"
#include "managed_adapter_test.h"
#include "motor/motor.h"
#include "serial_handle.h"

namespace encos {
namespace {

class PseudoTerminal {
public:
    PseudoTerminal() {
        fd_ = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) {
            throw std::runtime_error("posix_openpt failed");
        }
        if (grantpt(fd_) != 0 || unlockpt(fd_) != 0 || ptsname(fd_) == nullptr) {
            close(fd_);
            throw std::runtime_error("pseudo terminal setup failed");
        }
        path = ptsname(fd_);
    }
    ~PseudoTerminal() {
        Disconnect();
    }
    void Disconnect() {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }
    std::vector<uint8_t> Read(int timeout_ms) const {
        pollfd descriptor{fd_, POLLIN, 0};
        if (poll(&descriptor, 1, timeout_ms) <= 0) {
            return {};
        }
        std::array<uint8_t, 1024> buffer{};
        const auto size = read(fd_, buffer.data(), buffer.size());
        if (size <= 0) {
            return {};
        }
        return {buffer.begin(), buffer.begin() + size};
    }
    void Write(const std::vector<uint8_t>& data) const {
        ASSERT_EQ(write(fd_, data.data(), data.size()), static_cast<ssize_t>(data.size()));
    }
    std::string path;

private:
    int fd_;
};

class TestUsbSerialAdapter : public UsbSerialAdapter {
public:
    explicit TestUsbSerialAdapter(const std::string& path)
        : UsbSerialAdapter(path, "UsbSerialAdapterTest", LogLevel::Off) {}
    using UsbSerialAdapter::Send;
};

TEST(UsbSerialAdapterTests, SendsOnceWithoutWaitingForInput) {
    PseudoTerminal terminal;
    auto adapter = MakeManagedAdapter<TestUsbSerialAdapter>(terminal.path);
    MotorMessage message{};
    message.data.id = 1;
    message.data.len = 2;
    message.data.data[0] = 0xE0;
    message.data.data[1] = 1;
    const auto start = std::chrono::steady_clock::now();
    adapter->Send(message);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(100));
    EXPECT_EQ(terminal.Read(100), (std::vector<uint8_t>{0xAA, 0, 1, 7, 0xE0, 1, 0x93}));
    EXPECT_TRUE(terminal.Read(50).empty());
}

TEST(UsbSerialAdapterTests, DecodesFragmentedParameterReply) {
    PseudoTerminal terminal;
    auto adapter = MakeManagedAdapter<TestUsbSerialAdapter>(terminal.path);
    auto* motor = adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2);
    auto result = std::async(std::launch::async, [motor]() {
        return motor->GetParameter<MotorParameter::Position>();
    });
    EXPECT_EQ(terminal.Read(500), (std::vector<uint8_t>{0xAA, 0, 1, 7, 0xE0, 1, 0x93}));
    terminal.Write({0xAA, 0, 1});
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    terminal.Write({0x0B, 0xA0, 1, 0, 0, 0, 0, 0x57});
    ASSERT_EQ(result.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    EXPECT_FLOAT_EQ(result.get(), 0.0f);
}

TEST(UsbSerialAdapterTests, ClosesWhileReceiverWaitsForMoreBytes) {
    PseudoTerminal terminal;
    auto adapter = MakeManagedAdapter<TestUsbSerialAdapter>(terminal.path);
    terminal.Write({0xAA, 0, 1});
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto start = std::chrono::steady_clock::now();
    adapter = ManagedAdapterGuard<TestUsbSerialAdapter>(nullptr);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(500));
}

TEST(UsbSerialAdapterTests, DecodesCoalescedRepliesAfterCorruptFrame) {
    PseudoTerminal terminal;
    auto adapter = MakeManagedAdapter<TestUsbSerialAdapter>(terminal.path);
    auto* first = adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2);
    auto* second = adapter->GetBus(0)->GetMotor(2, MotorModel::EC_A4310_P2);
    auto result1 = std::async(std::launch::async, [first]() {
        return first->GetParameter<MotorParameter::Position>();
    });
    auto result2 = std::async(std::launch::async, [second]() {
        return second->GetParameter<MotorParameter::Position>();
    });
    auto requests = terminal.Read(500);
    if (requests.size() < 14) {
        const auto rest = terminal.Read(500);
        requests.insert(requests.end(), rest.begin(), rest.end());
    }
    EXPECT_EQ(requests.size(), 14);
    terminal.Write({0xAA, 0,    1,    5, 0, 0xAA, 0,    1, 0x0B, 0xA0, 1, 0, 0,   0,
                    0,    0x57, 0xAA, 0, 2, 0x0B, 0xA0, 1, 0,    0,    0, 0, 0x58});
    EXPECT_FLOAT_EQ(result1.get(), 0.0f);
    EXPECT_FLOAT_EQ(result2.get(), 0.0f);
}

TEST(UsbSerialAdapterTests, DisconnectMarksAdapterUnavailable) {
    PseudoTerminal terminal;
    auto adapter = MakeManagedAdapter<TestUsbSerialAdapter>(terminal.path);
    terminal.Disconnect();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (adapter->Ok() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_FALSE(adapter->Ok());
}

TEST(SerialPortCallbackTests, ReceivesWithoutWaitingForBufferAndRejectsRestart) {
    PseudoTerminal terminal;
    SerialPort port(terminal.path);
    std::promise<std::vector<std::byte>> received;
    auto result = received.get_future();
    port.Start([&](const std::byte* data, std::size_t size) {
        received.set_value({data, data + size});
    });
    EXPECT_THROW(port.Start([](const std::byte*, std::size_t) {}), std::logic_error);
    terminal.Write({1, 2, 3});
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    port.Close();
    EXPECT_EQ(result.get(), (std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}}));
    EXPECT_FALSE(port.Ok());
    EXPECT_FALSE(port.Write("x", 1));
    port.Close();
}

TEST(SerialPortCallbackTests, CloseWaitsForActiveCallback) {
    PseudoTerminal terminal;
    SerialPort port(terminal.path);
    std::promise<void> entered;
    std::promise<void> release;
    auto entered_result = entered.get_future();
    auto release_result = release.get_future();
    port.Start([&](const std::byte*, std::size_t) {
        entered.set_value();
        release_result.wait();
    });
    terminal.Write({1});
    EXPECT_EQ(entered_result.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    auto closed = std::async(std::launch::async, [&]() {
        port.Close();
    });
    EXPECT_EQ(closed.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);
    release.set_value();
    EXPECT_EQ(closed.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
}

TEST(SerialPortCallbackTests, DisconnectNotifiesAndStopsWrites) {
    PseudoTerminal terminal;
    SerialPort port(terminal.path);
    std::promise<void> disconnected;
    auto result = disconnected.get_future();
    port.Start([](const std::byte*, std::size_t) {},
               [&]() {
                   disconnected.set_value();
               });
    terminal.Disconnect();
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    EXPECT_FALSE(port.Ok());
    EXPECT_FALSE(port.Write("x", 1));
    port.Close();
}

TEST(SerialPortCallbackTests, CallbackExceptionStopsReceiver) {
    PseudoTerminal terminal;
    SerialPort port(terminal.path);
    std::promise<void> disconnected;
    auto result = disconnected.get_future();
    port.Start(
        [](const std::byte*, std::size_t) {
            throw std::runtime_error("callback failed");
        },
        [&]() {
            disconnected.set_value();
        });
    terminal.Write({1});
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    EXPECT_FALSE(port.Ok());
    port.Close();
}

TEST(UsbSerialPortTests, ReturnsAvailableBytesWithoutFillingBuffer) {
    PseudoTerminal terminal;
    SerialHandle port(terminal.path);
    terminal.Write({1, 2, 3});
    std::array<uint8_t, 4096> buffer{};
    EXPECT_EQ(port.Read(buffer.data(), buffer.size()), 3);
    EXPECT_EQ(buffer[2], 3);
}

TEST(UsbSerialPortTests, ResetsInheritedStopBitsAndSoftwareFlowControl) {
    PseudoTerminal terminal;
    const int descriptor = open(terminal.path.c_str(), O_RDWR | O_NOCTTY);
    ASSERT_GE(descriptor, 0);
    termios options{};
    ASSERT_EQ(tcgetattr(descriptor, &options), 0);
    options.c_cflag |= CSTOPB;
    options.c_iflag |= IXON | IXOFF | IXANY;
    ASSERT_EQ(tcsetattr(descriptor, TCSANOW, &options), 0);
    SerialHandle port(terminal.path);
    EXPECT_EQ(tcgetattr(descriptor, &options), 0);
    EXPECT_EQ(options.c_cflag & CSTOPB, 0U);
    EXPECT_EQ(options.c_iflag & (IXON | IXOFF | IXANY), 0U);
    close(descriptor);
}

TEST(UsbSerialPortTests, CancelWakesIdleRead) {
    PseudoTerminal terminal;
    SerialHandle port(terminal.path);
    auto result = std::async(std::launch::async, [&port]() {
        std::array<uint8_t, 16> buffer{};
        return port.Read(buffer.data(), buffer.size());
    });
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);
    port.Cancel();
    ASSERT_EQ(result.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    EXPECT_EQ(result.get(), 0);
}

TEST(UsbSerialPortTests, CancelInterruptsWriteBackpressure) {
    PseudoTerminal terminal;
    SerialHandle port(terminal.path);
    auto result = std::async(std::launch::async, [&port]() {
        const std::vector<uint8_t> bytes(1024 * 1024, 0x42);
        return port.Write(bytes.data(), bytes.size());
    });
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);
    port.Cancel();
    ASSERT_EQ(result.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    EXPECT_FALSE(result.get());
}

}  // namespace
}  // namespace encos
