// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>

#include "adapter/adapter_plugin.h"
#include "adapter/base_adapter.h"
#include "can_handle.h"
#include "can_socket_setup.h"
#include "export.h"

namespace encos {

/**
 * @brief CAN 适配器
 *
 * 通过一次性 fd broker 子进程初始化 SocketCAN fd，后续在主进程直接收发。
 */
class CanAdapter : public BaseAdapter {
public:
    /**
     * @brief 创建 CAN 适配器实例
     * @param interface_name CAN 接口名称（如 "can0", "vcan0"）
     * @param logger_name 日志记录器名称
     * @param log_level 日志级别
     * @return 适配器指针，调用者需负责管理生命周期
     */
    static CanAdapter* Create(const std::string& interface_name,
                              const std::string& logger_name = "CanAdapter",
                              LogLevel log_level = LogLevel::Info);

    /**
     * @brief 创建 CAN 适配器实例并传入扩展配置
     * @param interface_name CAN 接口名称（如 "can0", "vcan0"）
     * @param logger_name 日志记录器名称
     * @param log_level 日志级别
     * @param extend_json 宿主规范化的 JSON 配置串；空串等同空对象
     * @return 适配器指针
     * @throws std::runtime_error 位时序字段非法或接口初始化失败
     *
     * 识别字段 `bitrate`（仲裁段位率）、`sample_point`（仲裁段采样点，0-1 小数）、
     * `dbitrate`（数据段位率）与 `dsample_point`（数据段采样点）；缺省字段沿用默认值。
     */
    static CanAdapter* CreateWithExtend(const std::string& interface_name,
                                        const std::string& logger_name, LogLevel log_level,
                                        const std::string& extend_json);

    CanAdapter(const CanAdapter&) = delete;
    CanAdapter& operator=(const CanAdapter&) = delete;
    virtual ~CanAdapter() override;

    virtual std::unordered_map<int, Bus*> GetBuses() override;
    virtual bool Ok() override;

protected:
    CanAdapter(const std::string& interface_name, const std::string& logger_name,
               LogLevel log_level, const can::CanTiming& timing);
    void Stop();
    void Loop();

    virtual void Send(const MotorMessage& message) override;

private:
    int RequestSocketFromBroker(const can::CanTiming& timing) const;

    std::thread loop_thread_;               /**< 接收循环线程 */
    std::unique_ptr<CanHandle> can_handle_; /**< CAN 通信句柄 */
    std::atomic<bool> running_{false};      /**< 运行标志 */
};

/**
 * @brief 枚举可用的 SocketCAN 接口
 * @param emit 接口标识回调
 * @param context 调用方上下文
 */
void EnumerateCanInterfaces(AdapterInterfaceEmit emit, void* context);

}  // namespace encos

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 插件入口函数：创建适配器实例
 */
#ifndef ENCOS_STATIC_MODE
ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(
    const char* interface_name, const char* logger_name = "CanAdapter",
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
