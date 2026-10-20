// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "test_claim_device.h"

#include <memory>

#include "device/device_plugin_builder.h"

namespace encos {

TestClaimDevice::TestClaimDevice(const DeviceCreateContext& context) : Device(context) {}

void TestClaimDevice::OnMessage(const MotorPackMsg& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++update_count_;
    last_message_ = message;
}

int TestClaimDevice::UpdateCount() {
    auto operation = AcquireOperation();
    std::lock_guard<std::mutex> lock(mutex_);
    return update_count_;
}

std::optional<MotorPackMsg> TestClaimDevice::LastMessage() {
    auto operation = AcquireOperation();
    std::lock_guard<std::mutex> lock(mutex_);
    return last_message_;
}

const DeviceDriverDescriptorV1* MakeTestClaimDeviceDriver() {
    static const auto plugin =
        DevicePlugin<TestClaimDevice>("TestClaimDevice")
            .ClaimBus()
            .Create([](const DeviceCreateContext& context, const nlohmann::json&) {
                return std::make_unique<TestClaimDevice>(context);
            });
    return plugin.descriptor();
}

}  // namespace encos
