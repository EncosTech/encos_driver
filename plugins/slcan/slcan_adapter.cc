// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "slcan_adapter.h"

#include <iomanip>
#include <sstream>

#include "bus/bus.h"

namespace encos {

std::unordered_map<int, Bus*> SlcanAdapter::GetBuses() {
    return {{0, GetBus(0)}};
}

bool SlcanAdapter::Ok() {
    return serial_port_ && serial_port_->Ok();
}

void SlcanAdapter::Send(const MotorMessage& message) {
    if (!Ok()) {
        Logger()->error("Attempted to Send message on closed or invalid serial port");
        return;
    }
    const MotorPackMsg& msg = message.data;
    const uint8_t flags = SanitizeCanFrameFlags(msg.frame_flags);
    if (!CanFrameFlagsHaveValidCanFdBits(flags)) {
        Logger()->error("Invalid CAN FD flag bits in SLCAN message: {:#04x}", flags);
        return;
    }
    if (CanFrameFlagsUseCanFd(flags)) {
        Logger()->error("SLCAN does not support CAN FD frames; dropping message id={:#x}", msg.id);
        return;
    }
    if (msg.len > 8) {
        Logger()->error("SLCAN frame length exceeds 8 bytes: {}", msg.len);
        return;
    }

    const bool extended = CanFrameFlagsUseExtendedId(flags);
    const bool rtr = CanFrameFlagsUseRtr(flags);
    const uint32_t max_id = extended ? 0x1FFFFFFFu : 0x7FFu;
    if (msg.id > max_id) {
        Logger()->error("CAN id {:#x} exceeds {} frame range", msg.id,
                        extended ? "extended" : "standard");
        return;
    }

    std::stringstream ss;
    const char command_type = extended ? (rtr ? 'R' : 'T') : (rtr ? 'r' : 't');
    ss << command_type << std::hex << std::setfill('0') << std::setw(extended ? 8 : 3) << msg.id
       << std::setw(1) << static_cast<int>(msg.len);
    if (!rtr) {
        for (size_t i = 0; i < msg.len; ++i) {
            ss << std::setw(2) << static_cast<int>(static_cast<uint8_t>(msg.data[i]));
        }
    }
    ss << '\r';
    std::string command = ss.str();
    if (!serial_port_->Write(command.data(), command.size())) {
        Logger()->warn("Failed to write SLCAN command to serial port");
    }
}

SlcanAdapter::SlcanAdapter(const std::string& interface_name, const std::string& logger_name,
                           encos::LogLevel log_level)
    : BaseAdapter(interface_name, logger_name, log_level) {
    serial_port_ = std::make_unique<SerialPort>(interface_name);
    Logger()->info("Opened serial device {} successfully", interface_name);
    if (!serial_port_->Write("S8\r", 3)) {
        throw std::runtime_error("Failed to set SLCAN bitrate");
    }
    if (!serial_port_->Write("O\r", 2)) {
        throw std::runtime_error("Failed to open SLCAN");
    }

    serial_port_->Start(
        [this](const std::byte* data, std::size_t size) {
            ReceiveBytes(data, size);
        },
        {},
        [this]() {
            Logger()->warn("Failed to set SLCAN loop thread priority");
        });
}

SlcanAdapter::~SlcanAdapter() {
    if (serial_port_) {
        serial_port_->Write("C\r", 2);
        serial_port_->Close();
    }
    Logger()->info("Closed serial device {}", GetInterfaceName());
}

void SlcanAdapter::ReceiveBytes(const std::byte* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        const char byte = static_cast<char>(data[i]);
        if (byte == '\r' || byte == '\n') {
            if (!discarding_line_) {
                const std::string line(read_buffer_.data(), read_buf_size_);
                read_buf_size_ = 0;
                ReceiveLine(line);
            }
            read_buf_size_ = 0;
            discarding_line_ = false;
        } else if (!discarding_line_) {
            if (read_buf_size_ == read_buffer_.size()) {
                read_buf_size_ = 0;
                discarding_line_ = true;
            } else {
                read_buffer_[read_buf_size_++] = byte;
            }
        }
    }
}

void SlcanAdapter::ReceiveLine(const std::string& line) {
    if (!line.empty() && (line[0] == 't' || line[0] == 'r' || line[0] == 'T' || line[0] == 'R')) {
        try {
            const bool extended = (line[0] == 'T' || line[0] == 'R');
            const bool rtr = (line[0] == 'r' || line[0] == 'R');
            const std::size_t id_width = extended ? 8 : 3;
            const std::size_t len_index = 1 + id_width;
            const std::size_t data_start = len_index + 1;
            if (line.length() < data_start)
                return;

            std::string id_str = line.substr(1, id_width);
            std::string len_str = line.substr(len_index, 1);
            uint32_t id = std::stoi(id_str, nullptr, 16);
            int len = std::stoi(len_str, nullptr, 16);
            if (len > 8)
                len = 8;
            const std::size_t data_len = rtr ? 0 : static_cast<std::size_t>(len) * 2;
            if (line.length() < data_start + data_len)
                return;

            MotorMessage message;
            message.bus_idx = 0;
            MotorPackMsg msg{};
            msg.id = id;
            uint8_t flags = 0;
            if (extended) {
                flags |= kCanFrameFlagEff;
            }
            if (rtr) {
                flags |= kCanFrameFlagRtr;
            }
            msg.frame_flags = SanitizeCanFrameFlags(flags);
            msg.len = static_cast<uint8_t>(len);
            if (!rtr) {
                for (int j = 0; j < len; ++j) {
                    std::string byte_str =
                        line.substr(data_start + static_cast<std::size_t>(j) * 2, 2);
                    int byte = std::stoi(byte_str, nullptr, 16);
                    msg.data[j] = static_cast<uint8_t>(byte);
                }
            }
            message.data = msg;
            OnMessage(MotorMessages{message});
        } catch (const std::exception& e) {
            ENCOS_LOG_DEBUG(Logger(), "Failed to parse SLCAN line: {}", line);
        }
    }
}

SlcanAdapter* SlcanAdapter::Create(const std::string& interface_name,
                                   const std::string& logger_name, encos::LogLevel log_level) {
    return new SlcanAdapter(interface_name, logger_name, log_level);
}

}  // namespace encos
