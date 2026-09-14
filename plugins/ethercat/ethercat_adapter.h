#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

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

    EthercatAdapter(const EthercatAdapter&) = delete;
    EthercatAdapter& operator=(const EthercatAdapter&) = delete;
    virtual ~EthercatAdapter() override;

    virtual std::unordered_map<int, Bus*> GetBuses() override;
    virtual bool Ok() override;

protected:
    EthercatAdapter(const std::string& interface_name,
                    const std::string& logger_name = "EthercatAdapter",
                    LogLevel log_level = LogLevel::Info);
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
