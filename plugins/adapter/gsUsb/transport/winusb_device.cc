// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "winusb_device.h"

#ifndef _WIN32
#error "The GsUsb plugin transport is Windows only"
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
// clang-format off
#include <windows.h>
#include <winusb.h>
// clang-format on

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "winusb_enumeration.h"

namespace encos::gs_usb {

namespace {

constexpr ULONG kReadTimeoutMs = 200;

bool ControlTransfer(WINUSB_INTERFACE_HANDLE interface_handle, bool input, std::uint8_t request,
                     std::uint16_t value, std::uint16_t index, void* buffer, std::size_t size) {
    WINUSB_SETUP_PACKET setup{};
    setup.RequestType = input ? 0xC1U : 0x41U;
    setup.Request = request;
    setup.Value = value;
    setup.Index = index;
    setup.Length = static_cast<std::uint16_t>(size);
    ULONG transferred = 0;
    if (!WinUsb_ControlTransfer(interface_handle, setup, static_cast<PUCHAR>(buffer),
                                static_cast<ULONG>(size), &transferred, nullptr)) {
        return false;
    }
    return transferred == size;
}

}  // namespace

struct WinUsbDevice::Impl {
    HANDLE file = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE interface_handle = nullptr;
    UCHAR out_pipe = 0;
    UCHAR in_pipe = 0;
    unsigned channels = 0;
    HANDLE read_event = nullptr;
    HANDLE write_event = nullptr;
    std::string identifier;

    ~Impl() {
        if (interface_handle != nullptr) {
            WinUsb_Free(interface_handle);
        }
        if (read_event != nullptr) {
            CloseHandle(read_event);
        }
        if (write_event != nullptr) {
            CloseHandle(write_event);
        }
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
    }
};

WinUsbDevice::WinUsbDevice(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

WinUsbDevice::~WinUsbDevice() = default;

unsigned WinUsbDevice::ChannelCount() const {
    return impl_->channels;
}

const std::string& WinUsbDevice::Identifier() const {
    return impl_->identifier;
}

std::shared_ptr<WinUsbDevice> WinUsbDevice::Open(const std::string& identifier,
                                                 std::string* error) {
    const auto set_error = [&](const char* message) -> std::shared_ptr<WinUsbDevice> {
        if (error != nullptr) {
            *error = message;
        }
        return nullptr;
    };

    std::string path;
    for (const auto& device : EnumerateDevices()) {
        if (device.serial == identifier || device.path == identifier) {
            path = device.path;
            break;
        }
    }
    if (path.empty()) {
        return set_error("gs_usb device not found");
    }

    auto impl = std::make_unique<Impl>();
    impl->identifier = identifier;
    impl->file =
        CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (impl->file == INVALID_HANDLE_VALUE) {
        return set_error("cannot open gs_usb device");
    }
    if (!WinUsb_Initialize(impl->file, &impl->interface_handle)) {
        return set_error("WinUsb_Initialize failed");
    }

    USB_INTERFACE_DESCRIPTOR descriptor{};
    if (!WinUsb_QueryInterfaceSettings(impl->interface_handle, 0, &descriptor)) {
        return set_error("WinUsb_QueryInterfaceSettings failed");
    }
    for (std::uint8_t i = 0; i < descriptor.bNumEndpoints; ++i) {
        WINUSB_PIPE_INFORMATION pipe{};
        if (!WinUsb_QueryPipe(impl->interface_handle, 0, i, &pipe)) {
            continue;
        }
        if (pipe.PipeType != UsbdPipeTypeBulk) {
            continue;
        }
        if (USB_ENDPOINT_DIRECTION_IN(pipe.PipeId)) {
            impl->in_pipe = pipe.PipeId;
        } else {
            impl->out_pipe = pipe.PipeId;
        }
    }
    if (impl->in_pipe == 0 || impl->out_pipe == 0) {
        return set_error("gs_usb bulk endpoints not found");
    }

    std::array<std::uint8_t, 12> config{};
    if (!ControlTransfer(impl->interface_handle, true, kRqDeviceConfig, 0, 0, config.data(),
                         config.size())) {
        return set_error("gs_usb DEVICE_CONFIG failed");
    }
    impl->channels = static_cast<unsigned>(config[3]) + 1U;
    if (impl->channels == 0 || impl->channels > 8) {
        return set_error("gs_usb channel count invalid");
    }

    impl->read_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    impl->write_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (impl->read_event == nullptr || impl->write_event == nullptr) {
        return set_error("gs_usb event creation failed");
    }
    return std::shared_ptr<WinUsbDevice>(new WinUsbDevice(std::move(impl)));
}

bool WinUsbDevice::WriteHostFormat() {
    const std::uint32_t magic = kHostFormatMagic;
    return ControlTransfer(impl_->interface_handle, false, kRqHostFormat, 0, 0,
                           const_cast<std::uint32_t*>(&magic), sizeof(magic));
}

bool WinUsbDevice::ReadBtConstExt(BtConstExt* out) {
    std::array<std::uint8_t, 72> buffer{};
    if (!ControlTransfer(impl_->interface_handle, true, kRqBtConstExt, 0, 0, buffer.data(),
                         buffer.size())) {
        return false;
    }
    return DecodeBtConstExt(buffer.data(), buffer.size(), out);
}

bool WinUsbDevice::WriteBitTiming(const BitTiming& timing, bool data_phase, unsigned channel) {
    std::uint8_t buffer[20] = {};
    if (!EncodeBitTiming(timing, buffer)) {
        return false;
    }
    return ControlTransfer(impl_->interface_handle, false,
                           data_phase ? kRqDataBitTiming : kRqBitTiming,
                           static_cast<std::uint16_t>(channel), 0, buffer, sizeof(buffer));
}

bool WinUsbDevice::StartChannel(unsigned channel, bool fd, bool listen_only, bool loopback) {
    std::uint32_t mode = 1U;
    std::uint32_t flags = 0U;
    if (fd) {
        flags |= kModeFd;
    }
    if (listen_only) {
        flags |= kModeListenOnly;
    }
    if (loopback) {
        flags |= kModeLoopback;
    }
    std::uint8_t buffer[8] = {};
    WriteLe32(buffer, mode);
    WriteLe32(buffer + 4, flags);
    return ControlTransfer(impl_->interface_handle, false, kRqMode,
                           static_cast<std::uint16_t>(channel), 0, buffer, sizeof(buffer));
}

bool WinUsbDevice::StopChannel(unsigned channel) {
    std::uint8_t buffer[8] = {};
    WriteLe32(buffer, 0U);
    WriteLe32(buffer + 4, 0U);
    return ControlTransfer(impl_->interface_handle, false, kRqMode,
                           static_cast<std::uint16_t>(channel), 0, buffer, sizeof(buffer));
}

bool WinUsbDevice::ReadState(unsigned channel, ChannelState* out) {
    std::array<std::uint8_t, 12> buffer{};
    if (!ControlTransfer(impl_->interface_handle, true, kRqGetState,
                         static_cast<std::uint16_t>(channel), 0, buffer.data(), buffer.size())) {
        return false;
    }
    out->state = ReadLe32(buffer.data());
    out->rec = ReadLe32(buffer.data() + 4);
    out->tec = ReadLe32(buffer.data() + 8);
    return true;
}

bool WinUsbDevice::Write(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return false;
    }
    OVERLAPPED operation{};
    operation.hEvent = impl_->write_event;
    ResetEvent(operation.hEvent);
    ULONG written = 0;
    if (!WinUsb_WritePipe(impl_->interface_handle, impl_->out_pipe, const_cast<PUCHAR>(data),
                          static_cast<ULONG>(size), &written, &operation)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            return false;
        }
        if (WaitForSingleObject(operation.hEvent, 2000U) != WAIT_OBJECT_0) {
            CancelIoEx(impl_->file, &operation);
            return false;
        }
        if (!WinUsb_GetOverlappedResult(impl_->interface_handle, &operation, &written, FALSE)) {
            return false;
        }
    }
    return written == size;
}

int WinUsbDevice::Read(std::uint8_t* data, std::size_t capacity) {
    if (data == nullptr || capacity == 0) {
        return -1;
    }
    OVERLAPPED operation{};
    operation.hEvent = impl_->read_event;
    ResetEvent(operation.hEvent);
    ULONG transferred = 0;
    if (!WinUsb_ReadPipe(impl_->interface_handle, impl_->in_pipe, data,
                         static_cast<ULONG>(capacity), &transferred, &operation)) {
        const DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            return (error == ERROR_OPERATION_ABORTED) ? 0 : -1;
        }
        const DWORD wait = WaitForSingleObject(operation.hEvent, kReadTimeoutMs);
        if (wait != WAIT_OBJECT_0) {
            CancelIoEx(impl_->file, &operation);
            (void) WaitForSingleObject(operation.hEvent, INFINITE);
            return 0;
        }
        if (!WinUsb_GetOverlappedResult(impl_->interface_handle, &operation, &transferred, FALSE)) {
            const DWORD result_error = GetLastError();
            return (result_error == ERROR_OPERATION_ABORTED) ? 0 : -1;
        }
    }
    return static_cast<int>(transferred);
}

void WinUsbDevice::AbortRead() {
    if (impl_->file != INVALID_HANDLE_VALUE) {
        (void) CancelIoEx(impl_->file, nullptr);
    }
}

}  // namespace encos::gs_usb
