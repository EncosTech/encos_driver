// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "device/motor/types.h"
#include "encos/driver_manager.h"
#include "encos/export.h"
#include "platform/log.h"

namespace encos {

class Bus;

/** @brief 核心构造外部设备时提供的上下文 */
struct DeviceCreateContext {
    Bus* bus = nullptr;    /**< 设备所属总线 */
    std::string type_name; /**< 设备类型名 */
    /** 设备自定义配置：宿主规范化序列化后的 JSON 字符串；调用方未传配置时为 "{}" */
    std::string config;
    LoggerPtr logger;                                /**< 核心日志记录器 */
    std::function<void(const MotorPackMsg&)> writer; /**< 核心提供且绑定总线的发送通道 */
};

/**
 * @brief 外部设备插件基类
 *
 * 插件实现的公开方法 MUST 在入口调用 `AcquireOperation()` 以获得与内置设备一致的在飞
 * 请求保护；`OnMessage` 由核心接收路由在其在飞计数保护下调用，MUST NOT 调用
 * `AcquireOperation()`。存在请求-响应等待的插件 SHOULD 覆写 `CancelWaiters()`，以便
 * 设备销毁时唤醒等待者。
 */
class ENCOS_BASE_API Device {
public:
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    virtual ~Device();

    /**
     * @brief 核心接收线程的路由入口
     * @param message 匹配到该设备路由 ID 的一帧
     */
    virtual void OnMessage(const MotorPackMsg& message) = 0;

    /** @brief 取消并唤醒正在等待响应的调用者；由核心在销毁排空前调用 */
    virtual void CancelWaiters() {}

    /** @brief 设备类型名 */
    const std::string& Type() const;
    /** @brief 设备自定义配置（宿主序列化后的 JSON 字符串） */
    const std::string& Config() const;
    /** @brief 设备所属总线 */
    Bus* GetBus() const;

    /**
     * @brief 通过核心绑定的发送通道发送一帧
     * @param message 要发送的帧
     */
    void Send(const MotorPackMsg& message);

protected:
    /**
     * @brief 构造外部设备
     * @param context 核心提供的上下文
     */
    explicit Device(const DeviceCreateContext& context);

    /** @brief 获取核心日志记录器 */
    LoggerPtr Logger() const;

    /**
     * @brief 进入设备在飞保护；每个公开方法入口 MUST 调用
     * @return 作用域结束时自动退出的保护对象
     */
    auto AcquireOperation() {
        return EncosDriverManager::Instance().AcquireDeviceOperation(this);
    }

private:
    friend class EncosDriverManager;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos
