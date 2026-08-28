#include "slcan_adapter.h"

#include <chrono>
#include <iomanip>
#include <sstream>

#include "bus/bus.h"
#include "platform/delay.h"
#include "utils/thread_priority.h"

namespace encos {

std::unordered_map<int, Bus*> SlcanAdapter::GetBuses() {
    return {{0, GetBus(0)}};
}

bool SlcanAdapter::Ok() {
    platform::LockGuard<platform::Mutex> lock(serial_mutex_);
    return serial_port_ && serial_port_->isDeviceOpen();
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
    platform::LockGuard<platform::Mutex> lock(serial_mutex_);
    int ret = serial_port_->writeString(command.c_str());
    if (ret != 1) {
        Logger()->warn("Failed to write SLCAN command to serial port: error code {}", ret);
    }
}

SlcanAdapter::SlcanAdapter(const std::string& interface_name, const std::string& logger_name,
                           encos::LogLevel log_level)
    : BaseAdapter(interface_name, logger_name, log_level) {
    serial_mutex_.lock();
    serial_port_ = std::make_shared<serialib>();
    auto ret = serial_port_->openDevice(interface_name.c_str(), 115200);
    if (ret != 1) {
        Logger()->error("Failed to open serial device {}: error code {}", interface_name, ret);
        throw std::runtime_error("Failed to open serial device");
    } else {
        Logger()->info("Opened serial device {} successfully", interface_name);
    }
    // Set CAN bitrate to 1M (S8)
    ret = serial_port_->writeString("S8\r");
    if (ret != 1) {
        Logger()->error("Failed to set SLCAN bitrate: error code {}", ret);
        throw std::runtime_error("Failed to set SLCAN bitrate");
    }
    // Open SLCAN
    ret = serial_port_->writeString("O\r");
    if (ret != 1) {
        Logger()->error("Failed to open SLCAN: error code {}", ret);
        throw std::runtime_error("Failed to open SLCAN");
    }
    serial_mutex_.unlock();

    loop_thread_ = std::thread(&SlcanAdapter::Loop, this);
    platform::SleepFor(std::chrono::milliseconds(100));
}

SlcanAdapter::~SlcanAdapter() {
    loop_stopping_.store(true, std::memory_order_release);
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
    if (Ok()) {
        platform::LockGuard<platform::Mutex> lock(serial_mutex_);
        serial_port_->writeString("C\r");  // Close SLCAN
        serial_port_->closeDevice();
        serial_port_.reset();
        Logger()->info("Closed serial device {}", GetInterfaceName());
    }
}

void SlcanAdapter::Loop() {
    if (!utils::SetCurrentThreadPriority(50)) {
        Logger()->warn("Failed to set SLCAN loop thread priority");
    }
    if (!Ok()) {
        Logger()->error("Serial port not open in Loop");
        return;
    }
    while (!loop_stopping_.load(std::memory_order_acquire) && Ok()) {
        auto loop_start = std::chrono::steady_clock::now();
        int read_bytes = serial_port_->readBytes(
            read_buffer_.data() + read_buf_size_,
            static_cast<unsigned int>(read_buffer_.size() - read_buf_size_), 1, 100);
        if (read_bytes < 0) {
            continue;
        }
        read_buf_size_ += static_cast<uint16_t>(read_bytes);
        uint16_t start = 0;
        for (uint16_t i = 0; i < read_buf_size_; ++i) {
            if (read_buffer_[i] == '\r' || read_buffer_[i] == '\n') {
                std::string line(read_buffer_.data() + start, i - start);
                if (!line.empty() &&
                    (line[0] == 't' || line[0] == 'r' || line[0] == 'T' || line[0] == 'R')) {
                    // Parse SLCAN frame
                    try {
                        const bool extended = (line[0] == 'T' || line[0] == 'R');
                        const bool rtr = (line[0] == 'r' || line[0] == 'R');
                        const std::size_t id_width = extended ? 8 : 3;
                        const std::size_t len_index = 1 + id_width;
                        const std::size_t data_start = len_index + 1;
                        if (line.length() < data_start)
                            continue;

                        std::string id_str = line.substr(1, id_width);
                        std::string len_str = line.substr(len_index, 1);
                        uint32_t id = std::stoi(id_str, nullptr, 16);
                        int len = std::stoi(len_str, nullptr, 16);
                        if (len > 8)
                            len = 8;
                        const std::size_t data_len = rtr ? 0 : static_cast<std::size_t>(len) * 2;
                        if (line.length() < data_start + data_len)
                            continue;

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
                start = i + 1;
                i = start - 1;  // Adjust for Loop increment
            }
        }
        if (start < read_buf_size_) {
            std::memmove(read_buffer_.data(), read_buffer_.data() + start, read_buf_size_ - start);
            read_buf_size_ -= start;
        } else {
            // 所有已接收字节均已处理，避免下一轮重复解析旧帧。
            read_buf_size_ = 0;
        }
        auto end = std::chrono::steady_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(end - loop_start).count();
        if (duration < 1000) {
            platform::SleepFor(std::chrono::microseconds(1000 - duration));
        }
    }
}

SlcanAdapter* SlcanAdapter::Create(const std::string& interface_name,
                                   const std::string& logger_name, encos::LogLevel log_level) {
    return new SlcanAdapter(interface_name, logger_name, log_level);
}

}  // namespace encos
