// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <mutex>
#include <string>

#include "device/device.h"

namespace hello {

/** @brief 样例设备：记录收到的帧与创建时传入的配置字符串 */
class Hello : public encos::Device {
public:
    explicit Hello(const encos::DeviceCreateContext& context) : Device(context) {}

    void OnMessage(const encos::MotorPackMsg&) override;

    /** @brief 已接收帧数量；入口取得在飞操作保护 */
    int UpdateCount();

private:
    std::mutex mutex_;
    int update_count_ = 0;
};

}  // namespace hello
