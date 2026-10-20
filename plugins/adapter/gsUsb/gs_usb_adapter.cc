// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "gs_usb_adapter.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

#include "adapter/adapter_extend.h"
#include "bus/bus.h"
#include "gs_usb_timing.h"
#include "transport/winusb_enumeration.h"
#include "utils/adapter_extend_update.h"

namespace encos {

namespace {

constexpr int kWriteAttempts = 20;

}  // namespace

GsUsbAdapter::GsUsbAdapter(const std::string& interface_name, const std::string& logger_name,
                           LogLevel log_level, const AdapterExtend& extend)
    : BaseAdapter(interface_name, logger_name, log_level) {
    std::string error;
    device_ = gs_usb::WinUsbDevice::Open(interface_name, &error);
    if (!device_) {
        throw std::runtime_error("Failed to open gs_usb device '" + interface_name + "': " + error);
    }
    channels_ = device_->ChannelCount();

    // 总线标识为 (0, channel)：通道数在打开设备后才确定，因此在此校验信封
    for (const auto& [slave, bus] : extend.DeclaredBuses()) {
        if (slave != 0 || bus < 0 || static_cast<unsigned>(bus) >= channels_) {
            throw std::runtime_error("GsUsb adapter has no bus '" + std::to_string(slave) + ":" +
                                     std::to_string(bus) + "'");
        }
    }
    const auto timings = gs_usb::BuildChannelTimings(extend, channels_);
    timings_ = timings;

    if (!device_->WriteHostFormat()) {
        throw std::runtime_error("gs_usb HOST_FORMAT failed");
    }
    gs_usb::BtConstExt constraints;
    if (!device_->ReadBtConstExt(&constraints)) {
        throw std::runtime_error("gs_usb BT_CONST_EXT failed");
    }
    constraints_ = constraints;
    // 先复位每个通道（MODE stop），清除上一次异常退出遗留的 started 状态。
    for (unsigned channel = 0; channel < channels_; ++channel) {
        (void) device_->StopChannel(channel);
    }
    for (unsigned channel = 0; channel < channels_; ++channel) {
        const auto& timing = timings[channel];
        const auto nominal = gs_usb::ComputeBitTiming(constraints, timing.nominal_bps,
                                                      timing.nominal_sample_point, false);
        const auto data =
            gs_usb::ComputeBitTiming(constraints, timing.data_bps, timing.data_sample_point, true);
        if (!nominal.has_value() || !data.has_value()) {
            throw std::runtime_error("gs_usb cannot compute CAN FD bit timing for channel " +
                                     std::to_string(channel));
        }
        if (!device_->WriteBitTiming(*nominal, false, channel) ||
            !device_->WriteBitTiming(*data, true, channel)) {
            throw std::runtime_error("gs_usb BITTIMING failed for channel " +
                                     std::to_string(channel));
        }
        if (!device_->StartChannel(channel, true, false, false)) {
            throw std::runtime_error("gs_usb MODE start failed for channel " +
                                     std::to_string(channel));
        }
        Logger()->info("gs_usb channel {}: {} bps / {:.3f} sample, {} bps / {:.3f} (FD)", channel,
                       timing.nominal_bps, timing.nominal_sample_point, timing.data_bps,
                       timing.data_sample_point);
    }

    running_.store(true);
    receiver_ = std::thread(&GsUsbAdapter::ReceiveLoop, this);
    Logger()->info("gs_usb adapter started: {} channels CAN FD", channels_);
}

GsUsbAdapter::~GsUsbAdapter() {
    running_.store(false);
    if (device_) {
        device_->AbortRead();
    }
    if (receiver_.joinable()) {
        receiver_.join();
    }
    if (device_) {
        for (unsigned channel = 0; channel < channels_; ++channel) {
            (void) device_->StopChannel(channel);
        }
    }
    device_.reset();
}

bool GsUsbAdapter::UpdateBusExtend(const std::string& extend_json) {
    const auto extend = AdapterExtend::Parse(extend_json);
    adapter_update::WarnIgnoredCommonFields(extend, Logger());
    for (const auto& bus : extend.DeclaredBuses()) {
        const auto* override_json = extend.BusOverride(bus.first, bus.second);
        if (override_json == nullptr) {
            continue;
        }
        if (bus.first != 0 || bus.second < 0 || static_cast<unsigned>(bus.second) >= channels_) {
            throw std::runtime_error("GsUsb adapter has no bus '" + std::to_string(bus.first) +
                                     ":" + std::to_string(bus.second) + "'");
        }
        const unsigned channel = static_cast<unsigned>(bus.second);
        gs_usb::ChannelTiming timing;
        {
            std::lock_guard<std::mutex> lock(timing_mutex_);
            timing = timings_[channel];
        }
        gs_usb::ApplyChannelTiming(*override_json, &timing);
        const auto nominal = gs_usb::ComputeBitTiming(constraints_, timing.nominal_bps,
                                                      timing.nominal_sample_point, false);
        const auto data =
            gs_usb::ComputeBitTiming(constraints_, timing.data_bps, timing.data_sample_point, true);
        if (!nominal.has_value() || !data.has_value()) {
            throw std::runtime_error("gs_usb cannot compute CAN FD bit timing for channel " +
                                     std::to_string(channel));
        }
        // 控制传输不在 write_mutex_ 的保护范围内（那是批量 OUT 热路径），这里用独立的
        // 锁把 stop → 写时序 → start 作为一个整体保护起来。
        {
            std::lock_guard<std::mutex> lock(timing_mutex_);
            if (!device_->StopChannel(channel) ||
                !device_->WriteBitTiming(*nominal, false, channel) ||
                !device_->WriteBitTiming(*data, true, channel) ||
                !device_->StartChannel(channel, true, false, false)) {
                throw std::runtime_error("gs_usb channel " + std::to_string(channel) +
                                         " reconfiguration failed");
            }
            timings_[channel] = timing;
        }
        Logger()->info("gs_usb channel {} timing updated: {} bps / {}, {} bps / {} (FD)", channel,
                       timing.nominal_bps, timing.nominal_sample_point, timing.data_bps,
                       timing.data_sample_point);
    }
    return true;
}

std::unordered_map<int, Bus*> GsUsbAdapter::GetBuses() {
    std::unordered_map<int, Bus*> buses;
    for (unsigned channel = 0; channel < channels_; ++channel) {
        buses[static_cast<int>(channel)] = GetBus(static_cast<int>(channel));
    }
    return buses;
}

bool GsUsbAdapter::Ok() {
    return device_ != nullptr && running_.load();
}

bool GsUsbAdapter::WriteBuffer(const std::uint8_t* data, std::size_t size) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    for (int attempt = 0; attempt < kWriteAttempts; ++attempt) {
        if (!running_.load()) {
            return false;
        }
        if (device_->Write(data, size)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return false;
}

bool GsUsbAdapter::WriteFrame(const MotorMessage& message) {
    const MotorPackMsg& pack = message.data;
    const std::uint8_t flags = SanitizeCanFrameFlags(pack.frame_flags);
    const bool extended = CanFrameFlagsUseExtendedId(flags);
    const bool rtr = CanFrameFlagsUseRtr(flags);
    const bool fd = CanFrameFlagsUseCanFd(flags);

    gs_usb::Frame frame;
    // 该固件在 gs_frame_valid 中要求 echo_id < 10，否则会丢弃主机下发的帧。
    frame.echo_id = (echo_counter_.fetch_add(1, std::memory_order_relaxed) % 9U) + 1U;
    frame.can_id = pack.id & (extended ? gs_usb::kCanExtIdMask : gs_usb::kCanStdIdMask);
    if (extended) {
        frame.can_id |= gs_usb::kCanEff;
    }
    if (rtr) {
        frame.can_id |= gs_usb::kCanRtr;
    }
    frame.dlc = static_cast<std::uint8_t>(std::min<std::size_t>(pack.len, 8U));
    frame.channel = static_cast<std::uint8_t>(message.bus_idx);
    if (fd) {
        frame.flags |= gs_usb::kFrameFlagFd | gs_usb::kFrameFlagBrs;
    }
    std::memcpy(frame.data, pack.data, frame.dlc);

    std::uint8_t buffer[gs_usb::kFdFrameSize] = {};
    const std::size_t size = gs_usb::EncodeFrame(frame, buffer, sizeof(buffer));
    if (size == 0U) {
        Logger()->error("gs_usb dropped invalid frame id={:#x}", pack.id);
        return false;
    }
    if (!WriteBuffer(buffer, size)) {
        Logger()->warn("gs_usb write failed id={:#x} channel={}", pack.id, frame.channel);
        return false;
    }
    return true;
}

void GsUsbAdapter::Send(const MotorMessage& message) {
    if (!Ok()) {
        Logger()->error("Attempted to send on a closed gs_usb device");
        return;
    }
    (void) WriteFrame(message);
}

void GsUsbAdapter::Send(const MotorMessages& messages) {
    if (!Ok()) {
        Logger()->error("Attempted to send on a closed gs_usb device");
        return;
    }
    for (const auto& message : messages) {
        (void) WriteFrame(message);
    }
}

void GsUsbAdapter::SendSynchronized(const MotorMessages& messages) {
    Send(messages);
}

void GsUsbAdapter::ReceiveLoop() {
    std::vector<std::uint8_t> buffer(4096);
    while (running_.load()) {
        const int size = device_->Read(buffer.data(), buffer.size());
        if (size < 0) {
            running_.store(false);
            Logger()->error("gs_usb receive failed; adapter unavailable");
            return;
        }
        if (size == 0) {
            continue;
        }
        std::size_t offset = 0;
        while (offset < static_cast<std::size_t>(size)) {
            gs_usb::Frame frame;
            const std::size_t consumed = gs_usb::DecodeFrame(
                buffer.data() + offset, static_cast<std::size_t>(size) - offset, &frame);
            if (consumed == 0U) {
                break;
            }
            offset += consumed;
            if (frame.channel >= channels_) {
                continue;
            }
            if (frame.echo_id != gs_usb::kEchoBusFrame) {
                continue;  // TX 回显
            }
            if ((frame.can_id & gs_usb::kCanErr) != 0U) {
                continue;  // 错误帧暂不映射为电机消息
            }
            const bool extended = (frame.can_id & gs_usb::kCanEff) != 0U;
            const bool rtr = (frame.can_id & gs_usb::kCanRtr) != 0U;
            MotorMessage message{};
            message.bus_idx = frame.channel;
            MotorPackMsg pack{};
            pack.id = frame.can_id & (extended ? gs_usb::kCanExtIdMask : gs_usb::kCanStdIdMask);
            std::uint8_t flags = 0;
            if (extended) {
                flags |= kCanFrameFlagEff;
            }
            if (rtr) {
                flags |= kCanFrameFlagRtr;
            }
            if ((frame.flags & gs_usb::kFrameFlagFd) != 0U) {
                flags |= kCanFrameFlagFdMask;
            }
            pack.frame_flags = SanitizeCanFrameFlags(flags);
            pack.len = static_cast<std::uint8_t>(std::min<std::size_t>(frame.dlc, 8U));
            std::memcpy(pack.data, frame.data, pack.len);
            message.data = pack;
            OnMessage(MotorMessages{message});
        }
    }
}

GsUsbAdapter* GsUsbAdapter::Create(const std::string& interface_name,
                                   const std::string& logger_name, LogLevel log_level) {
    return new GsUsbAdapter(interface_name, logger_name, log_level, AdapterExtend{});
}

GsUsbAdapter* GsUsbAdapter::CreateWithExtend(const std::string& interface_name,
                                             const std::string& logger_name, LogLevel log_level,
                                             const std::string& extend_json) {
    return new GsUsbAdapter(interface_name, logger_name, log_level,
                            AdapterExtend::Parse(extend_json));
}

void EnumerateGsUsbInterfaces(AdapterInterfaceEmit emit, void* context) {
    if (emit == nullptr) {
        return;
    }
    for (const auto& device : gs_usb::EnumerateDevices()) {
        emit(device.serial.c_str(), context);
    }
}

}  // namespace encos
