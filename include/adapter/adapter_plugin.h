// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

/**
 * @file adapter_plugin.h
 * @brief 适配器插件 ABI：接口枚举
 *
 * 每个适配器插件都必须导出 EnumerateInterfaces，由插件加载器在列举可用接口时调用。
 * 插件自行决定枚举策略（可调用 platform 提供的共享 OS 接口，或使用插件内部专用实现）。
 */

namespace encos {

/** @brief 接口枚举回调；name 为接口标识（NUL 结尾），context 为调用方上下文 */
using AdapterInterfaceEmit = void (*)(const char* name, void* context);

/** @brief 适配器插件必须导出 EnumerateInterfaces 的函数类型 */
using EnumerateAdapterInterfacesFunc = void(AdapterInterfaceEmit emit, void* context);

}  // namespace encos
