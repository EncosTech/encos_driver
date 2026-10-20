// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

#include "device/device.h"
#include "device/device_plugin.h"

namespace encos {

/**
 * @brief 测试用外部设备，记录收到的帧以供断言
 */
class TestDevice : public Device {
public:
    explicit TestDevice(const DeviceCreateContext& context);

    void OnMessage(const MotorPackMsg& message) override;
    void CancelWaiters() override;

    /** @brief 已接收帧数量；入口取得在飞操作保护 */
    int UpdateCount();
    /** @brief 最近接收的一帧；入口取得在飞操作保护 */
    std::optional<MotorPackMsg> LastMessage();

private:
    std::mutex mutex_;
    int update_count_ = 0;
    std::optional<MotorPackMsg> last_message_;
    bool cancelled_ = false;
};

/** @brief 供静态注册表使用的设备驱动工厂 */
const DeviceDriverDescriptorV1* MakeTestDeviceDriver();

}  // namespace encos
