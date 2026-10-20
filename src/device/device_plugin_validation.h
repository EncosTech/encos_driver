// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <stdexcept>
#include <string>

#include "device/device_plugin.h"

namespace encos {

/**
 * @brief 校验设备驱动描述符
 *
 * 动态与静态加载器共用同一实现，确保两种构建模式对插件的接受条件一致。
 *
 * @param device_type 设备类型名，用于诊断
 * @param descriptor 插件返回的描述符
 * @throws std::runtime_error 描述符为空、ABI 版本不匹配或缺少必需函数指针时抛出
 */
inline void ValidateDeviceDescriptor(const std::string& device_type,
                                     const DeviceDriverDescriptorV1* descriptor) {
    if (descriptor == nullptr) {
        throw std::runtime_error("PluginLoader: Device plugin returned a null descriptor: " +
                                 device_type);
    }
    if (descriptor->abi_version != kDeviceDriverAbiVersion) {
        throw std::runtime_error("PluginLoader: Device plugin ABI mismatch for " + device_type +
                                 " (got " + std::to_string(descriptor->abi_version) +
                                 ", expected " + std::to_string(kDeviceDriverAbiVersion) + ")");
    }
    if (descriptor->type_name == nullptr || descriptor->fill_route_ids == nullptr ||
        descriptor->create == nullptr || descriptor->destroy == nullptr) {
        throw std::runtime_error("PluginLoader: Device plugin descriptor is incomplete: " +
                                 device_type);
    }
    if (device_type != descriptor->type_name) {
        throw std::runtime_error("PluginLoader: Device plugin type mismatch: requested '" +
                                 device_type + "' but plugin declares '" +
                                 std::string(descriptor->type_name) + "'");
    }
}

}  // namespace encos
