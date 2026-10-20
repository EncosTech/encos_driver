// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "winusb_enumeration.h"

#ifndef _WIN32
#error "The GsUsb plugin transport is Windows only"
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
// clang-format off
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
// clang-format on

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace encos::gs_usb {

namespace {

std::string Utf8(const wchar_t* value) {
    if (value == nullptr) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::optional<GUID> ParseGuid(const std::wstring& text) {
    unsigned long data1 = 0;
    unsigned long data2 = 0;
    unsigned long data3 = 0;
    unsigned long bytes[8] = {};
    const wchar_t* current = text.c_str();
    if (*current == L'{') {
        ++current;
    }
    if (swscanf_s(current, L"%8lx-%4lx-%4lx-%2lx%2lx-%2lx%2lx%2lx%2lx%2lx%2lx", &data1, &data2,
                  &data3, &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5],
                  &bytes[6], &bytes[7]) != 11) {
        return std::nullopt;
    }
    GUID guid{};
    guid.Data1 = static_cast<unsigned long>(data1);
    guid.Data2 = static_cast<unsigned short>(data2);
    guid.Data3 = static_cast<unsigned short>(data3);
    for (std::size_t i = 0; i < 8U; ++i) {
        guid.Data4[i] = static_cast<unsigned char>(bytes[i]);
    }
    return guid;
}

/** @brief 读取设备的 USB 字符串描述符（iSerial/iProduct 等） */
std::string ReadStringDescriptor(WINUSB_INTERFACE_HANDLE interface_handle, std::uint8_t index) {
    if (index == 0U) {
        return {};
    }
    struct DescriptorBuffer {
        std::uint8_t length;
        std::uint8_t descriptor_type;
        wchar_t text[126];
    };
    DescriptorBuffer buffer{};
    WINUSB_SETUP_PACKET language{};
    language.RequestType = 0x80U;
    language.Request = 6U;
    language.Value = static_cast<std::uint16_t>((3U << 8U) | 0U);
    language.Index = 0x0409U;
    language.Length = static_cast<std::uint16_t>(sizeof(buffer));
    ULONG transferred = 0;
    if (!WinUsb_ControlTransfer(interface_handle, language, reinterpret_cast<PUCHAR>(&buffer),
                                sizeof(buffer), &transferred, nullptr) ||
        transferred < 4U) {
        return {};
    }
    const std::uint16_t language_id = *reinterpret_cast<std::uint16_t*>(buffer.text);
    DescriptorBuffer value{};
    WINUSB_SETUP_PACKET string{};
    string.RequestType = 0x80U;
    string.Request = 6U;
    string.Value = static_cast<std::uint16_t>((3U << 8U) | index);
    string.Index = language_id;
    string.Length = static_cast<std::uint16_t>(sizeof(value));
    if (!WinUsb_ControlTransfer(interface_handle, string, reinterpret_cast<PUCHAR>(&value),
                                sizeof(value), &transferred, nullptr) ||
        transferred < 2U) {
        return {};
    }
    return Utf8(value.text);
}

/** @brief 读取设备描述符中的 iSerialNumber / iProduct 索引 */
bool ReadDescriptorIndices(WINUSB_INTERFACE_HANDLE interface_handle, std::uint8_t* serial_index,
                           std::uint8_t* product_index) {
    struct DeviceDescriptor {
        std::uint8_t length;
        std::uint8_t descriptor_type;
        std::uint16_t bcd_usb;
        std::uint8_t device_class;
        std::uint8_t device_subclass;
        std::uint8_t device_protocol;
        std::uint8_t max_packet_size;
        std::uint16_t id_vendor;
        std::uint16_t id_product;
        std::uint16_t bcd_device;
        std::uint8_t manufacturer_index;
        std::uint8_t product_index;
        std::uint8_t serial_number_index;
    };
    DeviceDescriptor descriptor{};
    WINUSB_SETUP_PACKET setup{};
    setup.RequestType = 0x80U;
    setup.Request = 6U;
    setup.Value = 0x0100U;
    setup.Index = 0U;
    setup.Length = static_cast<std::uint16_t>(sizeof(descriptor));
    ULONG transferred = 0;
    if (!WinUsb_ControlTransfer(interface_handle, setup, reinterpret_cast<PUCHAR>(&descriptor),
                                sizeof(descriptor), &transferred, nullptr) ||
        transferred < 17U) {
        return false;
    }
    if (serial_index != nullptr) {
        *serial_index = descriptor.serial_number_index;
    }
    if (product_index != nullptr) {
        *product_index = descriptor.product_index;
    }
    return true;
}

bool HasVendorInterface(WINUSB_INTERFACE_HANDLE interface_handle) {
    USB_INTERFACE_DESCRIPTOR descriptor{};
    if (!WinUsb_QueryInterfaceSettings(interface_handle, 0, &descriptor)) {
        return false;
    }
    if (descriptor.bInterfaceClass != 0xFFU) {
        return false;
    }
    bool has_bulk_in = false;
    bool has_bulk_out = false;
    for (std::uint8_t i = 0; i < descriptor.bNumEndpoints; ++i) {
        WINUSB_PIPE_INFORMATION pipe{};
        if (!WinUsb_QueryPipe(interface_handle, 0, i, &pipe)) {
            continue;
        }
        if (pipe.PipeType == UsbdPipeTypeBulk) {
            if (USB_ENDPOINT_DIRECTION_IN(pipe.PipeId)) {
                has_bulk_in = true;
            } else {
                has_bulk_out = true;
            }
        }
    }
    return has_bulk_in && has_bulk_out;
}

/** @brief 用 gs_usb 的 HOST_FORMAT/DEVICE_CONFIG 探测设备，成功返回通道数 */
std::optional<unsigned> ProbeGsUsb(WINUSB_INTERFACE_HANDLE interface_handle) {
    const std::uint32_t magic = 0x0000beefU;
    WINUSB_SETUP_PACKET host_format{};
    host_format.RequestType = 0x41U;
    host_format.Request = 0U;
    host_format.Value = 0U;
    host_format.Index = 0U;
    host_format.Length = 4U;
    ULONG transferred = 0;
    if (!WinUsb_ControlTransfer(interface_handle, host_format,
                                reinterpret_cast<PUCHAR>(const_cast<std::uint32_t*>(&magic)), 4U,
                                &transferred, nullptr) ||
        transferred != 4U) {
        return std::nullopt;
    }
    std::uint8_t config[12] = {};
    WINUSB_SETUP_PACKET device_config{};
    device_config.RequestType = 0xC1U;
    device_config.Request = 5U;
    device_config.Value = 0U;
    device_config.Index = 0U;
    device_config.Length = sizeof(config);
    if (!WinUsb_ControlTransfer(interface_handle, device_config, config, sizeof(config),
                                &transferred, nullptr) ||
        transferred != sizeof(config)) {
        return std::nullopt;
    }
    const unsigned channels = static_cast<unsigned>(config[3]) + 1U;
    if (channels == 0U || channels > 8U) {
        return std::nullopt;
    }
    return channels;
}

std::string InstanceSerial(HDEVINFO devices, SP_DEVINFO_DATA* info) {
    wchar_t instance[512] = {};
    if (!SetupDiGetDeviceInstanceIdW(devices, info, instance,
                                     static_cast<DWORD>(std::size(instance)), nullptr)) {
        return {};
    }
    std::string id = Utf8(instance);
    const std::size_t separator = id.find_last_of('\\');
    if (separator == std::string::npos) {
        return {};
    }
    const std::string tail = id.substr(separator + 1);
    // 位置路径（含 '&'）不是序列号，避免误用。
    if (tail.find('&') != std::string::npos) {
        return {};
    }
    return tail;
}

std::vector<GUID> WinUsbInterfaceGuids() {
    std::vector<GUID> guids;
    HDEVINFO devices =
        SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
        return guids;
    }
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        if (!SetupDiEnumDeviceInfo(devices, index, &info)) {
            break;
        }
        wchar_t instance[512] = {};
        if (!SetupDiGetDeviceInstanceIdW(devices, &info, instance,
                                         static_cast<DWORD>(std::size(instance)), nullptr)) {
            continue;
        }
        // WCID 的 DeviceInterfaceGUIDs 位于设备硬件键：
        // HKLM\SYSTEM\CurrentControlSet\Enum\<instance>\Device Parameters
        const std::wstring path =
            std::wstring(L"SYSTEM\\CurrentControlSet\\Enum\\") + instance + L"\\Device Parameters";
        HKEY parameters = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &parameters) !=
            ERROR_SUCCESS) {
            continue;
        }
        wchar_t buffer[1024] = {};
        DWORD size = sizeof(buffer);
        DWORD type = 0;
        if (RegQueryValueExW(parameters, L"DeviceInterfaceGUIDs", nullptr, &type,
                             reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS &&
            (type == REG_MULTI_SZ || type == REG_SZ)) {
            for (const wchar_t* current = buffer; *current != L'\0';
                 current += wcslen(current) + 1) {
                if (const auto guid = ParseGuid(current); guid.has_value()) {
                    if (std::find_if(guids.begin(), guids.end(), [&](const GUID& existing) {
                            return IsEqualGUID(existing, *guid) != 0;
                        }) == guids.end()) {
                        guids.push_back(*guid);
                    }
                }
            }
        }
        RegCloseKey(parameters);
    }
    SetupDiDestroyDeviceInfoList(devices);
    return guids;
}

}  // namespace

std::vector<DeviceInfo> EnumerateDevices() {
    std::vector<DeviceInfo> result;
    const auto guids = WinUsbInterfaceGuids();
    for (const GUID& guid : guids) {
        HDEVINFO devices =
            SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devices == INVALID_HANDLE_VALUE) {
            continue;
        }
        for (DWORD index = 0;; ++index) {
            SP_DEVICE_INTERFACE_DATA interface_data{};
            interface_data.cbSize = sizeof(interface_data);
            if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &guid, index, &interface_data)) {
                break;
            }
            DWORD required = 0;
            SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, nullptr, 0, &required,
                                             nullptr);
            if (required == 0U) {
                continue;
            }
            std::vector<std::uint8_t> detail(required);
            auto* detail_data = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detail.data());
            detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            SP_DEVINFO_DATA info{};
            info.cbSize = sizeof(info);
            if (!SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, detail_data, required,
                                                  nullptr, &info)) {
                continue;
            }
            HANDLE file = CreateFileW(detail_data->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                continue;
            }
            WINUSB_INTERFACE_HANDLE interface_handle = nullptr;
            const bool initialized = WinUsb_Initialize(file, &interface_handle) != 0;
            if (!initialized) {
                CloseHandle(file);
                continue;
            }
            std::optional<unsigned> channels;
            if (HasVendorInterface(interface_handle)) {
                channels = ProbeGsUsb(interface_handle);
            }
            if (channels.has_value()) {
                DeviceInfo device;
                device.path = Utf8(detail_data->DevicePath);
                std::uint8_t serial_index = 0;
                std::uint8_t product_index = 0;
                if (ReadDescriptorIndices(interface_handle, &serial_index, &product_index)) {
                    device.serial = ReadStringDescriptor(interface_handle, serial_index);
                }
                if (device.serial.empty()) {
                    device.serial = InstanceSerial(devices, &info);
                }
                if (device.serial.empty()) {
                    device.serial = device.path;
                }
                result.push_back(std::move(device));
            }
            WinUsb_Free(interface_handle);
            CloseHandle(file);
        }
        SetupDiDestroyDeviceInfoList(devices);
    }
    return result;
}

}  // namespace encos::gs_usb
