// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

#include "device/device_plugin.h"
#include "encos/export.h"

namespace encos {

/** @brief 设备驱动解析函数：按类型名返回插件描述符 */
using DeviceDriverResolver = const DeviceDriverDescriptorV1* (*) (const std::string&);

/**
 * @brief 注册设备驱动解析函数
 *
 * 由已编译进主库的设备加载器在进程启动时注册；基础库的 `CreateDevice` 通过该
 * 解析函数定位插件描述符，从而避免基础库反向依赖主库。
 *
 * @param resolver 解析函数指针
 */
ENCOS_BASE_API void SetDeviceDriverResolver(DeviceDriverResolver resolver);

}  // namespace encos
