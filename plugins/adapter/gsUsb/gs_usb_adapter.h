// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "adapter/adapter_extend.h"
#include "adapter/adapter_plugin.h"
#include "adapter/base_adapter.h"
#include "export.h"
#include "gs_usb_timing.h"
#include "winusb_device.h"

namespace encos {

/**
 * @brief gs_usb（candleLight 兼容）USB-CAN 适配器
 *
 * 仅 Windows 可用：通过 WinUSB 访问 gs_usb 设备，按帧内通道号映射到多条 Bus。
 */
class GsUsbAdapter : public BaseAdapter {
public:
    static GsUsbAdapter* Create(const std::string& interface_name,
                                const std::string& logger_name = "GsUsbAdapter",
                                LogLevel log_level = LogLevel::Info);

    /**
     * @brief 创建适配器并接收 extend 配置串
     * @param interface_name 设备序列号
     * @param logger_name 日志记录器名称
     * @param log_level 日志级别
     * @param extend_json 宿主规范化的扩展配置串；空串等同空信封
     * @return 适配器指针
     * @throws std::runtime_error 信封结构非法、位时序字段非法、指向不存在的通道，或设备初始化失败
     *
     * 识别字段与 Can、Ethernet 插件统一：`bitrate`、`sample_point`、`dbitrate`、
     * `dsample_point`。总线标识为 `<0>:<channel>` 或其等价裸索引，可逐通道覆盖，例如
     * `{"bitrate": 1000000, "buses": {"0:6": {"bitrate": 250000}}}`
     */
    static GsUsbAdapter* CreateWithExtend(const std::string& interface_name,
                                          const std::string& logger_name, LogLevel log_level,
                                          const std::string& extend_json);

    GsUsbAdapter(const GsUsbAdapter&) = delete;
    GsUsbAdapter& operator=(const GsUsbAdapter&) = delete;
    ~GsUsbAdapter() override;

    std::unordered_map<int, Bus*> GetBuses() override;
    bool Ok() override;

    /**
     * @brief 按 `buses` 增量更新既有实例上被点名通道的位时序
     * @param extend_json 宿主规范化的扩展配置串
     * @return 全部点名通道都更新成功时返回 true
     * @throws std::runtime_error 信封非法、通道不存在、时序不可实现，或设备拒绝
     *
     * 顶层公共参数被忽略并记录诊断。重配会中断该通道上正在传输的报文。
     */
    bool UpdateBusExtend(const std::string& extend_json) override;

protected:
    GsUsbAdapter(const std::string& interface_name, const std::string& logger_name,
                 LogLevel log_level, const AdapterExtend& extend);
    void Send(const MotorMessage& message) override;
    void Send(const MotorMessages& messages) override;
    void SendSynchronized(const MotorMessages& messages) override;

private:
    void ReceiveLoop();
    bool WriteFrame(const MotorMessage& message);
    bool WriteBuffer(const std::uint8_t* data, std::size_t size);

    std::shared_ptr<gs_usb::WinUsbDevice> device_;
    unsigned channels_{0};
    /** 各通道当前生效的位时序，作为部分更新的合并种子 */
    std::vector<gs_usb::ChannelTiming> timings_;
    /** 设备上报的位时序常量，用于在构造之后重算 BitTiming */
    gs_usb::BtConstExt constraints_{};
    /** 只保护 timings_ 与 stop→写时序→start 这一组控制传输；不沿用批量 OUT 的 write_mutex_ */
    std::mutex timing_mutex_;
    std::atomic<bool> running_{false};
    std::thread receiver_;
    std::mutex write_mutex_;
    std::atomic<std::uint32_t> echo_counter_{1};
};

/**
 * @brief 枚举可用的 gs_usb 设备序列号
 * @param emit 接口标识回调
 * @param context 调用方上下文
 */
void EnumerateGsUsbInterfaces(AdapterInterfaceEmit emit, void* context);

}  // namespace encos

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ENCOS_STATIC_MODE
ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(
    const char* interface_name, const char* logger_name = "GsUsbAdapter",
    int log_level = static_cast<int>(encos::LogLevel::Info));
#endif

#ifdef __cplusplus
}
#endif
