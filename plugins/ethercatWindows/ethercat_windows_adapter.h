#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>

#include "adapter/base_adapter.h"
#include "ethercat_windows_handle.h"
#include "export.h"

namespace encos {

/**
 * @brief EtherCAT Windows 适配器
 *
 * 直接通过 SOEM 库实现 EtherCAT 主站功能，无需外部守护进程。
 * 适用于低延迟、高实时性要求的场景。
 */
class EthercatWindowsAdapter : public BaseAdapter {
public:
    /**
     * @brief 创建 EtherCAT 直接适配器实例
     * @param interface_name 网络接口名称（如 "eth0"）
     * @param logger_name 日志记录器名称
     * @param log_level 日志级别
     * @return 适配器指针
     */
    static EthercatWindowsAdapter* Create(const std::string& interface_name,
                                          const std::string& logger_name = "EthercatAdapter",
                                          LogLevel log_level = LogLevel::Info);

    EthercatWindowsAdapter(const EthercatWindowsAdapter&) = delete;
    EthercatWindowsAdapter& operator=(const EthercatWindowsAdapter&) = delete;
    virtual ~EthercatWindowsAdapter() override;

    virtual std::unordered_map<int, Bus*> GetBuses() override;
    virtual bool Ok() override;

protected:
    EthercatWindowsAdapter(const std::string& interface_name,
                           const std::string& logger_name = "EthercatAdapter",
                           LogLevel log_level = LogLevel::Info);

    virtual void Send(const MotorMessage& message) override;
    void SendSynchronized(const MotorMessages& messages) override;

private:
    std::shared_ptr<EthercatWindowsHandle> ec_master_; /**< EtherCAT 主站句柄 */
    std::thread loop_thread_;                          /**< 通信循环线程 */

    void Loop();
};

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
