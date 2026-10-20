// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "gs_usb_protocol.h"

namespace encos::gs_usb {

/**
 * @brief 单个 gs_usb 设备的 WinUSB 传输
 *
 * 通过平台枚举把序列号解析为 WinUSB 设备接口路径，再打开并完成控制/批量传输。
 */
class WinUsbDevice {
public:
    static std::shared_ptr<WinUsbDevice> Open(const std::string& identifier, std::string* error);

    ~WinUsbDevice();
    WinUsbDevice(const WinUsbDevice&) = delete;
    WinUsbDevice& operator=(const WinUsbDevice&) = delete;

    unsigned ChannelCount() const;
    const std::string& Identifier() const;

    bool WriteHostFormat();
    bool ReadBtConstExt(BtConstExt* out);
    bool WriteBitTiming(const BitTiming& timing, bool data_phase, unsigned channel);
    bool StartChannel(unsigned channel, bool fd, bool listen_only, bool loopback);
    bool StopChannel(unsigned channel);
    bool ReadState(unsigned channel, ChannelState* out);

    bool Write(const std::uint8_t* data, std::size_t size);
    /** @brief 阻塞读取一次；超时返回 0，错误返回 -1 */
    int Read(std::uint8_t* data, std::size_t capacity);
    void AbortRead();

private:
    struct Impl;
    explicit WinUsbDevice(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos::gs_usb
