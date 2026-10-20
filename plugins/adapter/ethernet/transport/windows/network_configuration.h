// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once
namespace encos::ethernet {
/** @brief 使用当前进程权限配置有线网卡地址与链路；成功返回零，需要管理员权限。 */
int ConfigureWindowsInterface(unsigned index);
}  // namespace encos::ethernet
