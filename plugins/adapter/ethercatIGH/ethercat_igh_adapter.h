// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <thread>
#include <unordered_map>

#include "adapter/adapter_extend.h"
#include "adapter/adapter_plugin.h"
#include "adapter/base_adapter.h"
#include "ethercat_igh_handle.h"
#include "export.h"

namespace encos {

/**
 * @brief IGH 主站的直连 adapter（不依赖外部 executable 进程）。
 *
 * 职责包括：
 * 1) 接管 handle 生命周期；
 * 2) 在 BaseAdapter 与 handle 之间转发收发消息；
 * 3) 基于 handle 的 bus 拓扑创建 Bus 对象。
 */
class EthercatIGHAdapter : public BaseAdapter {
public:
    /// 工厂函数，供插件入口 MakeAdapter 调用。
    static EthercatIGHAdapter* Create(const std::string& interface_name,
                                      const std::string& logger_name = "EthercatIGHAdapter",
                                      LogLevel log_level = LogLevel::Info);

    /**
     * @brief 工厂函数，额外接收逐通道 CAN 位时序
     * @param interface_name IGH master ID 的十进制字符串
     * @param logger_name 日志记录器名称
     * @param log_level 日志级别
     * @param extend_json 宿主规范化的扩展配置串；含位时序字段的通道注册 CAN 时序 SDO
     * @return 适配器指针
     */
    static EthercatIGHAdapter* CreateWithExtend(const std::string& interface_name,
                                                const std::string& logger_name, LogLevel log_level,
                                                const std::string& extend_json);

    EthercatIGHAdapter(const EthercatIGHAdapter&) = delete;
    EthercatIGHAdapter& operator=(const EthercatIGHAdapter&) = delete;
    ~EthercatIGHAdapter() override;

    std::unordered_map<int, Bus*> GetBuses() override;
    bool Ok() override;

    /**
     * @brief 按 `buses` 增量更新既有实例上被点名通道的 CAN 位时序
     * @param extend_json 宿主规范化的扩展配置串
     * @return 全部点名总线都更新成功时返回 true
     * @throws std::runtime_error 信封非法、总线不存在，或从站拒绝该时序
     *
     * 顶层公共参数被忽略并记录诊断。
     */
    bool UpdateBusExtend(const std::string& extend_json) override;

protected:
    EthercatIGHAdapter(const std::string& interface_name,
                       const std::string& logger_name = "EthercatIGHAdapter",
                       LogLevel log_level = LogLevel::Info,
                       const AdapterExtend& extend = AdapterExtend{});

    void Send(const MotorMessage& message) override;
    void Send(const MotorMessages& messages) override;
    void SendSynchronized(const MotorMessages& messages) override;

private:
    /// 独立线程运行 handle 的周期 Loop，避免阻塞上层调用线程。
    void Loop();

    std::shared_ptr<EthercatIGHHandle> ec_master_;
    std::thread loop_thread_;
};

/**
 * @brief 枚举可用的 IGH EtherCAT 主站
 * @param emit 接口标识回调
 * @param context 调用方上下文
 */
void EnumerateEthercatIghInterfaces(AdapterInterfaceEmit emit, void* context);

/**
 * @brief 创建 IGH 适配器并解析宿主传入的扩展配置串
 * @param interface_name IGH master ID 的十进制字符串
 * @param logger_name 日志记录器名称
 * @param log_level 日志级别
 * @param extend_json 宿主规范化的扩展配置串；空串等同空信封
 * @return 适配器指针
 * @throws std::runtime_error 扩展字段不受支持或取值非法
 */
BaseAdapter* CreateEthercatIghAdapterStaticWithExtend(const std::string& interface_name,
                                                      const std::string& logger_name,
                                                      LogLevel log_level,
                                                      const std::string& extend_json);

}  // namespace encos

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ENCOS_STATIC_MODE
ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(
    const char* interface_name, const char* logger_name = "EthercatIGHAdapter",
    int log_level = static_cast<int>(encos::LogLevel::Info));
#endif

#ifdef __cplusplus
}
#endif
