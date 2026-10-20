// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "adapter/adapter_extend.h"
#include "adapter/adapter_plugin.h"
#include "adapter/base_adapter.h"
#include "ethercat_handle.h"
#include "export.h"

namespace encos {

/**
 * @brief 跨平台 SOEM EtherCAT 适配器
 *
 * Linux 动态构建通过 broker 获取原始套接字，Windows 使用 SOEM 抓包后端。
 */
class EthercatAdapter : public BaseAdapter {
public:
    /**
     * @brief 创建 EtherCAT 适配器实例
     * @param interface_name 网络接口名称（如 "eth0"）
     * @param logger_name 日志记录器名称
     * @param log_level 日志级别
     * @return 适配器指针，调用者需负责管理生命周期
     */
    static EthercatAdapter* Create(const std::string& interface_name,
                                   const std::string& logger_name = "EthercatAdapter",
                                   LogLevel log_level = LogLevel::Info);

    /**
     * @brief 创建 EtherCAT 适配器实例并接收逐通道 CAN 位时序
     * @param interface_name 网络接口名称（如 "eth0"）
     * @param logger_name 日志记录器名称
     * @param log_level 日志级别
     * @param extend_json 宿主规范化的扩展配置串；含位时序字段的通道在 SAFE_OP 下发 SDO
     * @return 适配器指针，调用者需负责管理生命周期
     */
    static EthercatAdapter* CreateWithExtend(const std::string& interface_name,
                                             const std::string& logger_name, LogLevel log_level,
                                             const std::string& extend_json);

    EthercatAdapter(const EthercatAdapter&) = delete;
    EthercatAdapter& operator=(const EthercatAdapter&) = delete;
    virtual ~EthercatAdapter() override;

    virtual std::unordered_map<int, Bus*> GetBuses() override;
    virtual bool Ok() override;

    /**
     * @brief 按 `buses` 增量更新既有实例上被点名通道的 CAN 位时序
     * @param extend_json 宿主规范化的扩展配置串
     * @return 全部点名总线都更新成功时返回 true
     * @throws std::runtime_error 信封非法、总线不存在，或从站拒绝该时序
     *
     * 顶层公共参数被忽略并记录诊断。下发由 SOEM 循环线程执行，因此会占用目标周期；
     * 调用方应先停掉该总线的流量。
     */
    bool UpdateBusExtend(const std::string& extend_json) override;

protected:
    EthercatAdapter(const std::string& interface_name,
                    const std::string& logger_name = "EthercatAdapter",
                    LogLevel log_level = LogLevel::Info,
                    const AdapterExtend& extend = AdapterExtend{});
    void Stop();
    void Loop();

    virtual void Send(const MotorMessage& message) override;
    virtual void Send(const MotorMessages& messages) override;
    void SendSynchronized(const MotorMessages& messages) override;

private:
    std::shared_ptr<EthercatHandle> ec_master_; /**< EtherCAT 主站句柄 */
    std::thread loop_thread_;                   /**< 通信循环线程 */
    std::atomic<bool> running_{false};          /**< 运行标志 */
    std::vector<int> bus_sizes_;                /**< 各从站的总线大小 */
};

/**
 * @brief 枚举可用于 EtherCAT 的网卡接口
 * @param emit 接口标识回调
 * @param context 调用方上下文
 */
void EnumerateEthercatInterfaces(AdapterInterfaceEmit emit, void* context);

/**
 * @brief 创建 EtherCAT 适配器并解析宿主传入的扩展配置串
 * @param interface_name 网络接口名称
 * @param logger_name 日志记录器名称
 * @param log_level 日志级别
 * @param extend_json 宿主规范化的扩展配置串；空串等同空信封
 * @return 适配器指针
 * @throws std::runtime_error 扩展字段不受支持或取值非法，或从站未进入 OP
 */
BaseAdapter* CreateEthercatAdapterStaticWithExtend(const std::string& interface_name,
                                                   const std::string& logger_name, LogLevel level,
                                                   const std::string& extend_json);

}  // namespace encos

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 插件入口函数：创建适配器实例
 */
#ifndef ENCOS_STATIC_MODE
ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(
    const char* interface_name, const char* logger_name = "EthercatAdapter",
    int log_level = static_cast<int>(encos::LogLevel::Info));
#endif

#ifdef __cplusplus
}
#endif
