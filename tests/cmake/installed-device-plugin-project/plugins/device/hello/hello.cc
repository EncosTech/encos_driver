// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "hello.h"

#include <memory>

#include "device/device_plugin_builder.h"

namespace hello {

void Hello::OnMessage(const encos::MotorPackMsg&) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++update_count_;
}

int Hello::UpdateCount() {
    auto operation = AcquireOperation();
    std::lock_guard<std::mutex> lock(mutex_);
    return update_count_;
}

const encos::DeviceDriverDescriptorV1* MakeHelloDriver() {
    static const auto plugin =
        encos::DevicePlugin<Hello>("Hello")
            .Routes([](const nlohmann::json& config) {
                return std::vector<std::uint32_t>{config.value("route_id", 0x500u)};
            })
            .Create([](const encos::DeviceCreateContext& context, const nlohmann::json&) {
                return std::make_unique<Hello>(context);
            });
    return plugin.descriptor();
}

}  // namespace hello
