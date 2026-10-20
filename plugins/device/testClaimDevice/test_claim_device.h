// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <mutex>
#include <optional>

#include "device/device.h"
#include "device/device_plugin.h"

namespace encos {

/**
 * @brief 认领整条总线的测试设备，记录收到的每一帧以供断言
 *
 * 该设备声明认领能力位并在 `fill_route_ids` 中返回整总线标识符，因此接收本总线上所有
 * 未被其它设备路由命中的帧（含系统帧 `0x7FF`）。
 */
class TestClaimDevice : public Device {
public:
    explicit TestClaimDevice(const DeviceCreateContext& context);

    void OnMessage(const MotorPackMsg& message) override;

    /** @brief 已接收帧数量；入口取得在飞操作保护 */
    int UpdateCount();
    /** @brief 最近接收的一帧；入口取得在飞操作保护 */
    std::optional<MotorPackMsg> LastMessage();

private:
    std::mutex mutex_;
    int update_count_ = 0;
    std::optional<MotorPackMsg> last_message_;
};

/** @brief 供静态注册表使用的设备驱动工厂 */
const DeviceDriverDescriptorV1* MakeTestClaimDeviceDriver();

}  // namespace encos
