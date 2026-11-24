// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <string>

#include "external/json.hpp"
#include "wasm/wasm_runtime.h"
namespace encos::node {
using Json = nlohmann::json;
using Emit = std::function<void(const Json&)>;
Json InvokeMotor(wasm::RuntimeStore& store, const std::string& operation, const Json& args);
Json InvokeDevice(wasm::RuntimeStore& store, const std::string& operation, const Json& args);
Json InvokeFake(wasm::RuntimeStore& store, const std::string& operation, const Json& args);
void ClearNativeUtilities(wasm::RuntimeStore& store) noexcept;
Json InvokeUtility(wasm::RuntimeStore& store, const std::string& operation, const Json& args);
std::function<void()> BindDeviceCallback(wasm::RuntimeStore& store, const std::string& operation,
                                         const Json& args, Emit emit);
std::function<void()> BindFakeCallback(wasm::RuntimeStore& store, const std::string& operation,
                                       const Json& args, Emit emit);
Json InvokeNative(wasm::RuntimeStore& store, const std::string& operation, const Json& args);
std::function<void()> BindNativeCallback(wasm::RuntimeStore& store, const std::string& operation,
                                         const Json& args, Emit emit);
}  // namespace encos::node
