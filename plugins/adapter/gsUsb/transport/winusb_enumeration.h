// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

namespace encos::gs_usb {

/** @brief 发现的 gs_usb 设备标识 */
struct DeviceInfo {
    std::string serial; /**< 设备序列号（优先），用于对外标识 */
    std::string path;   /**< WinUSB 设备接口路径，用于打开设备 */
};

/**
 * @brief 枚举系统中所有 gs_usb 设备（不依赖 VID/PID）
 *
 * 通过 WinUSB 设备接口探测 gs_usb 的 HOST_FORMAT/DEVICE_CONFIG 请求来识别。
 */
std::vector<DeviceInfo> EnumerateDevices();

}  // namespace encos::gs_usb
