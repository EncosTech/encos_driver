// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "usb_serial_adapter.h"

#include <algorithm>
#include <cstring>

#include "bus/bus.h"
#include "utils/serial_crc.h"

namespace encos {

UsbSerialAdapter::UsbSerialAdapter(const std::string& interface_name,
                                   const std::string& logger_name, LogLevel log_level)
    : BaseAdapter(interface_name, logger_name, log_level),
      serial_port_(std::make_unique<SerialPort>(interface_name)) {
    serial_port_->Start(
        [this](const std::byte* data, std::size_t size) {
            ReceiveBytes(data, size);
        },
        [this]() {
            Logger()->warn("Serial receive failed or device disconnected");
        },
        [this]() {
            Logger()->warn("Failed to set USB serial loop thread priority");
        });
}

UsbSerialAdapter::~UsbSerialAdapter() {
    serial_port_->Close();
}

bool UsbSerialAdapter::Ok() {
    return serial_port_->Ok();
}

std::unordered_map<int, Bus*> UsbSerialAdapter::GetBuses() {
    return {{0, GetBus(0)}};
}

void UsbSerialAdapter::Send(const MotorMessage& message) {
    if (!Ok()) {
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
    if (!serial_port_->Write(buffer.data(), buffer.size())) {
        Logger()->warn("Failed to write complete message to serial port");
    }
}

void UsbSerialAdapter::ReceiveBytes(const std::byte* data, std::size_t size) {
    while (size > 0) {
        const auto count = std::min(size, read_buffer_.size() - read_buf_size_);
        std::memcpy(read_buffer_.data() + read_buf_size_, data, count);
        read_buf_size_ += static_cast<uint16_t>(count);
        data += count;
        size -= count;
        ParseBuffer();
    }
}

void UsbSerialAdapter::ParseBuffer() {
    MotorMessages messages;
    messages.reserve(read_buf_size_ / 5);
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
            const auto* packet = read_buffer_.data() + i;
            uint8_t checksum = 0;
            for (int j = 0; j < len - 1; ++j) {
                checksum += static_cast<uint8_t>(packet[j]);
            }
            const auto crc = static_cast<std::byte>(checksum);
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
            std::memcpy(msg.data, packet + header_size, msg.len);
            message.data = msg;
            messages.push_back(message);
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
    if (!messages.empty()) {
        OnMessage(messages);
    }
}

UsbSerialAdapter* UsbSerialAdapter::Create(const std::string& interface_name,
                                           const std::string& logger_name,
                                           encos::LogLevel log_level) {
    return new UsbSerialAdapter(interface_name, logger_name, log_level);
}

}  // namespace encos
