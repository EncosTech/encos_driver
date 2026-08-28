#include "usb_serial_adapter.h"

#include <chrono>

#include "bus/bus.h"
#include "platform/delay.h"
#include "utils/serial_crc.h"
#include "utils/thread_priority.h"

namespace encos {

std::unordered_map<int, Bus*> UsbSerialAdapter::GetBuses() {
    return {{0, GetBus(0)}};
}

bool UsbSerialAdapter::Ok() {
    platform::LockGuard<platform::Mutex> lock(serial_mutex_);
    return serial_port_ && serial_port_->isDeviceOpen();
}

void UsbSerialAdapter::Send(const MotorMessage& message) {
    if (!sender_ || !Ok()) {
        Logger()->error("Attempted to Send message on closed or invalid serial port");
        return;
    }
    const MotorPackMsg& msg = message.data;
    const uint8_t flags = SanitizeCanFrameFlags(msg.frame_flags);
    if (msg.len > 8) {
        Logger()->error("USB serial frame length exceeds 8 bytes: {}", msg.len);
        return;
    }

    const bool extended = CanFrameFlagsUseExtendedId(flags);
    const std::size_t header_size = extended ? 6 : 4;
    std::vector<std::byte> buffer(msg.len + header_size + 1, std::byte{0});
    buffer[0] = static_cast<std::byte>(extended ? 0xBB : 0xAA);
    if (extended) {
        buffer[1] = static_cast<std::byte>((msg.id >> 24) & 0xFF);
        buffer[2] = static_cast<std::byte>((msg.id >> 16) & 0xFF);
        buffer[3] = static_cast<std::byte>((msg.id >> 8) & 0xFF);
        buffer[4] = static_cast<std::byte>(msg.id & 0xFF);
        buffer[5] = static_cast<std::byte>(buffer.size());
    } else {
        buffer[1] = static_cast<std::byte>(msg.id >> 8);
        buffer[2] = static_cast<std::byte>(msg.id & 0xFF);
        buffer[3] = static_cast<std::byte>(buffer.size());
    }
    std::memcpy(buffer.data() + header_size, msg.data, msg.len);
    buffer[buffer.size() - 1] = SerialCrc::calc(buffer);
    sender_->Send(std::move(buffer));
}

UsbSerialAdapter::UsbSerialAdapter(const std::string& interface_name,
                                   const std::string& logger_name, encos::LogLevel log_level)
    : BaseAdapter(interface_name, logger_name, log_level) {
    {
        platform::LockGuard<platform::Mutex> lock(serial_mutex_);
        serial_port_ = std::make_shared<serialib>();
        auto ret = serial_port_->openDevice(interface_name.c_str(), 115200);
        if (ret != 1) {
            Logger()->error("Failed to open serial device {}: error code {}", interface_name, ret);
            throw std::runtime_error("Failed to open serial device");
        } else {
            Logger()->info("Opened serial device {} successfully", interface_name);
        }
    }

    sender_ = std::make_unique<UsbSerialSender>(
        [this](const UsbSerialSender::Frame& frame) {
            platform::LockGuard<platform::Mutex> lock(serial_mutex_);
            if (!serial_port_ || !serial_port_->isDeviceOpen()) {
                return 0;
            }
            return serial_port_->writeBytes(frame.data(), static_cast<unsigned int>(frame.size()));
        },
        Logger(), kMaxPendingRetries, std::chrono::milliseconds(3));
    running_.store(true);
    loop_thread_ = std::thread(&UsbSerialAdapter::Loop, this);
    platform::SleepFor(std::chrono::milliseconds(100));
}

UsbSerialAdapter::~UsbSerialAdapter() {
    if (sender_) {
        sender_->Stop();
        sender_.reset();
    }
    running_.store(false);
    {
        platform::LockGuard<platform::Mutex> lock(serial_mutex_);
        if (serial_port_) {
            serial_port_->closeDevice();
            serial_port_.reset();
            Logger()->info("Closed serial device {}", GetInterfaceName());
        }
    }
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
}

void UsbSerialAdapter::Loop() {
    if (!utils::SetCurrentThreadPriority(50)) {
        Logger()->warn("Failed to set USB serial loop thread priority");
    }
    if (!Ok()) {
        Logger()->error("Serial port not open in Loop");
        return;
    }
    while (running_.load() && Ok()) {
        auto start = std::chrono::steady_clock::now();
        int read_bytes = 0;
        {
            platform::LockGuard<platform::Mutex> lock(serial_mutex_);
            if (!serial_port_ || !serial_port_->isDeviceOpen()) {
                break;
            }
            read_bytes = serial_port_->readBytes(
                read_buffer_.data() + read_buf_size_,
                static_cast<unsigned int>(read_buffer_.size() - read_buf_size_), 1, 100);
        }
        if (read_bytes < 0) {
            continue;
        }
        read_buf_size_ += static_cast<uint16_t>(read_bytes);
        uint16_t i = 0;
        for (; i < read_buf_size_; ++i) {
            const bool standard = read_buffer_[i] == std::byte{0xAA};
            const bool extended = read_buffer_[i] == std::byte{0xBB};
            if (standard || extended) {
                const std::size_t header_size = extended ? 6 : 4;
                const std::size_t len_index = extended ? (i + 5) : (i + 3);
                if (len_index >= read_buf_size_) {
                    break;
                }
                uint8_t len = static_cast<uint8_t>(read_buffer_[len_index]);
                const uint8_t max_len = static_cast<uint8_t>(header_size + 8 + 1);
                if (len > max_len || len <= header_size) {
                    continue;
                }
                if (i + len > read_buf_size_) {
                    break;
                }
                std::vector<std::byte> packet(len - 1, std::byte{0});
                for (int j = 0; j < len - 1; ++j) {
                    packet[j] = read_buffer_[i + j];
                }
                std::byte crc = SerialCrc::calc(packet);
                if (crc != read_buffer_[i + len - 1]) {
                    ENCOS_LOG_DEBUG(Logger(), "CRC mismatch on received packet");
                    continue;
                }
                MotorMessage message;
                message.bus_idx = 0;
                MotorPackMsg msg{};
                if (extended) {
                    msg.id = (static_cast<uint32_t>(static_cast<uint8_t>(packet[1])) << 24) |
                             (static_cast<uint32_t>(static_cast<uint8_t>(packet[2])) << 16) |
                             (static_cast<uint32_t>(static_cast<uint8_t>(packet[3])) << 8) |
                             static_cast<uint32_t>(static_cast<uint8_t>(packet[4]));
                    msg.frame_flags = SanitizeCanFrameFlags(kCanFrameFlagEff);
                } else {
                    msg.id = (static_cast<uint16_t>(static_cast<uint8_t>(packet[1])) << 8) |
                             static_cast<uint16_t>(static_cast<uint8_t>(packet[2]));
                    msg.frame_flags = 0;
                }
                msg.len = static_cast<uint8_t>(len - header_size - 1);
                std::memcpy(msg.data, packet.data() + header_size, msg.len);
                message.data = msg;
                OnMessage(MotorMessages{message});
                i += len - 1;
            }
        }
        if (i < read_buf_size_) {
            std::memmove(read_buffer_.data(), read_buffer_.data() + i, read_buf_size_ - i);
            read_buf_size_ -= i;
        } else {
            // 所有已接收字节均已处理，避免下一轮重复解析旧帧。
            read_buf_size_ = 0;
        }
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        if (duration < 1000) {
            platform::SleepFor(std::chrono::microseconds(1000 - duration));
        }
    }
}

UsbSerialAdapter* UsbSerialAdapter::Create(const std::string& interface_name,
                                           const std::string& logger_name,
                                           encos::LogLevel log_level) {
    return new UsbSerialAdapter(interface_name, logger_name, log_level);
}

}  // namespace encos
