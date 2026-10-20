// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>

#include "adapter/adapter_plugin.h"
#include "adapter/base_adapter.h"
#include "export.h"
#include "serial_port.h"

namespace encos {

/**
 * @brief SLCAN（串行 CAN）适配器
 *
 * 通过 USB-CAN 转换器的串行协议实现 CAN 通信。
 * 适用于使用 SLCAN 协议的虚拟 CAN 设备。
 */
class SlcanAdapter : public BaseAdapter {
public:
    /**
     * @brief 创建 SLCAN 适配器实例
     * @param interface_name 串口设备路径（如 "/dev/tty_usb0"）
     * @param logger_name 日志记录器名称
     * @param log_level 日志级别
     * @return 适配器指针
     */
    static SlcanAdapter* Create(const std::string& interface_name,
                                const std::string& logger_name = "SlcanAdapter",
                                LogLevel log_level = LogLevel::Info);

    /**
     * @brief 创建 SLCAN 适配器实例并传入扩展配置
     * @param interface_name 串口设备路径（如 "/dev/tty_usb0"）
     * @param logger_name 日志记录器名称
     * @param log_level 日志级别
     * @param extend_json 宿主规范化的 JSON 配置串；空串等同空对象
     * @return 适配器指针
     * @throws std::runtime_error 配置含 SLCAN 不支持的字段、位率无法映射到 S0-S8，
     *         或串口打开/初始化失败
     *
     * 唯一识别字段为 `bitrate`，映射到 SLCAN 速率码 `S0`-`S8`；缺省为 1000000（`S8`）。
     * 串口链路波特率固定 115200，不开放配置。
     */
    static SlcanAdapter* CreateWithExtend(const std::string& interface_name,
                                          const std::string& logger_name, LogLevel log_level,
                                          const std::string& extend_json);

    SlcanAdapter(const SlcanAdapter&) = delete;
    SlcanAdapter& operator=(const SlcanAdapter&) = delete;
    virtual ~SlcanAdapter() override;

    virtual std::unordered_map<int, Bus*> GetBuses() override;
    virtual bool Ok() override;

protected:
    SlcanAdapter(const std::string& interface_name, const std::string& logger_name,
                 LogLevel log_level, const std::string& rate_code);

    virtual void Send(const MotorMessage& message) override;

private:
    std::unique_ptr<SerialPort> serial_port_; /**< 串口句柄 */
    std::array<char, 1024> read_buffer_;      /**< 读取缓冲区 */
    std::size_t read_buf_size_{0};            /**< 缓冲区数据大小 */
    bool discarding_line_{false};             /**< 丢弃超长帧直到下一行 */

    void ReceiveBytes(const std::byte* data, std::size_t size);
    void ReceiveLine(const std::string& line);
};

/**
 * @brief 枚举可用的 SLCAN 串口设备
 * @param emit 接口标识回调
 * @param context 调用方上下文
 */
void EnumerateSlcanInterfaces(AdapterInterfaceEmit emit, void* context);

}  // namespace encos

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 插件入口函数：创建适配器实例
 */
#ifndef ENCOS_STATIC_MODE
ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(
    const char* interface_name, const char* logger_name = "SlcanAdapter",
    int log_level = static_cast<int>(encos::LogLevel::Info));

/**
 * @brief 插件入口函数：创建适配器实例并接收 extend 配置串
 */
ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapterV2(const char* interface_name,
                                                   const char* logger_name, int log_level,
                                                   const char* extend_json);
#endif

#ifdef __cplusplus
}
#endif
