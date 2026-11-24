// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
#pragma once
#include "node/native_api.h"
namespace encos::node {
/** @brief 执行当前运行时持有的日志工具操作。 */
Json InvokeLogUtility(wasm::RuntimeStore& store, const std::string& operation, const Json& args);
/** @brief 销毁运行时的日志工具并刷新剩余数据。 */
void ClearNativeUtilities(wasm::RuntimeStore& store) noexcept;
}  // namespace encos::node
