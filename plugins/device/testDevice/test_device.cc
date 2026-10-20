// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "test_device.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "device/device_plugin_builder.h"
#include "external/json.hpp"

namespace encos {

namespace {

constexpr std::uint32_t kTestDeviceBaseId = 0x7A0u;

/** @brief 从配置读取 base_id；缺省或非法配置回退默认值 */
std::uint32_t ResolveBaseId(const nlohmann::json& config) {
    if (config.is_object() && config.contains("base_id")) {
        return config.at("base_id").get<std::uint32_t>();
    }
    return kTestDeviceBaseId;
}

}  // namespace

TestDevice::TestDevice(const DeviceCreateContext& context) : Device(context) {}

void TestDevice::OnMessage(const MotorPackMsg& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++update_count_;
    last_message_ = message;
}

void TestDevice::CancelWaiters() {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelled_ = true;
}

int TestDevice::UpdateCount() {
    auto operation = AcquireOperation();
    std::lock_guard<std::mutex> lock(mutex_);
    return update_count_;
}

std::optional<MotorPackMsg> TestDevice::LastMessage() {
    auto operation = AcquireOperation();
    std::lock_guard<std::mutex> lock(mutex_);
    return last_message_;
}

const DeviceDriverDescriptorV1* MakeTestDeviceDriver() {
    // 端到端样板：插件以现代 C++ 包装层编写，跨 .so 仍只暴露 C ABI 描述符。
    static const auto plugin =
        DevicePlugin<TestDevice>("TestDevice")
            .Routes([](const nlohmann::json& config) {
                return std::vector<std::uint32_t>{ResolveBaseId(config)};
            })
            .Create([](const DeviceCreateContext& context, const nlohmann::json&) {
                return std::make_unique<TestDevice>(context);
            });
    return plugin.descriptor();
}

}  // namespace encos
