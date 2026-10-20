// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "serial_handle.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <windows.h>

namespace encos {

struct SerialHandle::Impl {
    HANDLE port = INVALID_HANDLE_VALUE;
    HANDLE cancel = nullptr;
    HANDLE read_event = nullptr;
    HANDLE write_event = nullptr;

    ~Impl() {
        if (port != INVALID_HANDLE_VALUE) {
            CloseHandle(port);
        }
        if (read_event != nullptr) {
            CloseHandle(read_event);
        }
        if (write_event != nullptr) {
            CloseHandle(write_event);
        }
        if (cancel != nullptr) {
            CloseHandle(cancel);
        }
    }

    bool Cancelled() const {
        return WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0;
    }

    bool Complete(OVERLAPPED& operation, DWORD timeout, DWORD& transferred) {
        const HANDLE events[] = {cancel, operation.hEvent};
        if (WaitForMultipleObjects(2, events, FALSE, timeout) == WAIT_OBJECT_0 + 1) {
            return GetOverlappedResult(port, &operation, &transferred, FALSE) != FALSE;
        }
        CancelIoEx(port, &operation);
        // 取消是异步请求，必须等待完成后才能释放 OVERLAPPED 和调用方缓冲区。
        GetOverlappedResult(port, &operation, &transferred, TRUE);
        return false;
    }
};

SerialHandle::SerialHandle(const std::string& path) : impl_(std::make_unique<Impl>()) {
    std::string device = path;
    if (path.size() > 3 && (path[0] == 'C' || path[0] == 'c') &&
        (path[1] == 'O' || path[1] == 'o') && (path[2] == 'M' || path[2] == 'm') &&
        path.find_first_not_of("0123456789", 3) == std::string::npos) {
        device = "\\\\.\\" + path;
    }
    impl_->port = CreateFileA(device.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (impl_->port == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Cannot open serial port: " + path);
    }
    DCB config{};
    config.DCBlength = sizeof(config);
    if (!GetCommState(impl_->port, &config)) {
        throw std::runtime_error("Cannot get serial configuration: " + path);
    }
    config.BaudRate = CBR_115200;
    config.ByteSize = 8;
    config.Parity = NOPARITY;
    config.StopBits = ONESTOPBIT;
    config.fBinary = TRUE;
    config.fParity = FALSE;
    config.fOutxCtsFlow = FALSE;
    config.fOutxDsrFlow = FALSE;
    config.fDtrControl = DTR_CONTROL_DISABLE;
    config.fDsrSensitivity = FALSE;
    config.fTXContinueOnXoff = TRUE;
    config.fOutX = FALSE;
    config.fInX = FALSE;
    config.fErrorChar = FALSE;
    config.fNull = FALSE;
    config.fRtsControl = RTS_CONTROL_DISABLE;
    config.fAbortOnError = FALSE;
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    // 有数据立即返回；无数据等待首字节，超时后继续等待，取消事件独立唤醒。
    timeouts.ReadTotalTimeoutConstant = 1000;
    timeouts.WriteTotalTimeoutConstant = 100;
    if (!SetCommState(impl_->port, &config) || !SetCommTimeouts(impl_->port, &timeouts)) {
        throw std::runtime_error("Cannot configure serial port: " + path);
    }
    impl_->cancel = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    impl_->read_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    impl_->write_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (impl_->cancel == nullptr || impl_->read_event == nullptr || impl_->write_event == nullptr) {
        throw std::runtime_error("Cannot create serial events: " + path);
    }
}

SerialHandle::~SerialHandle() = default;

int SerialHandle::Read(void* buffer, std::size_t capacity) {
    if (buffer == nullptr || capacity == 0) {
        return -1;
    }
    const auto length = static_cast<DWORD>(
        std::min(capacity, static_cast<std::size_t>(std::numeric_limits<int>::max())));
    while (!impl_->Cancelled()) {
        OVERLAPPED operation{};
        operation.hEvent = impl_->read_event;
        ResetEvent(operation.hEvent);
        DWORD transferred = 0;
        const BOOL completed = ReadFile(impl_->port, buffer, length, &transferred, &operation);
        if (!completed && (GetLastError() != ERROR_IO_PENDING ||
                           !impl_->Complete(operation, INFINITE, transferred))) {
            return impl_->Cancelled() ? 0 : -1;
        }
        if (impl_->Cancelled()) {
            return 0;
        }
        if (transferred != 0) {
            return static_cast<int>(transferred);
        }
    }
    return 0;
}

bool SerialHandle::Write(const void* buffer, std::size_t size) {
    if (buffer == nullptr || size == 0 || impl_->Cancelled()) {
        return false;
    }
    const auto* bytes = static_cast<const unsigned char*>(buffer);
    const ULONGLONG deadline = GetTickCount64() + 100;
    std::size_t offset = 0;
    while (offset < size && !impl_->Cancelled()) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            return false;
        }
        OVERLAPPED operation{};
        operation.hEvent = impl_->write_event;
        ResetEvent(operation.hEvent);
        DWORD transferred = 0;
        const auto length =
            static_cast<DWORD>(std::min(size - offset, static_cast<std::size_t>(MAXDWORD)));
        const BOOL completed =
            WriteFile(impl_->port, bytes + offset, length, &transferred, &operation);
        if (!completed) {
            if (GetLastError() != ERROR_IO_PENDING) {
                return false;
            }
            const ULONGLONG submitted = GetTickCount64();
            const auto remaining =
                static_cast<DWORD>(submitted < deadline ? deadline - submitted : 0);
            if (!impl_->Complete(operation, remaining, transferred)) {
                return false;
            }
        }
        if (transferred == 0) {
            return false;
        }
        offset += transferred;
    }
    return offset == size && !impl_->Cancelled();
}

void SerialHandle::Cancel() noexcept {
    SetEvent(impl_->cancel);
    CancelIoEx(impl_->port, nullptr);
}

}  // namespace encos
