#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>

#include "adapter/base_adapter.h"
#include "can_handle.h"
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

    CanAdapter(const CanAdapter&) = delete;
    CanAdapter& operator=(const CanAdapter&) = delete;
    virtual ~CanAdapter() override;

    virtual std::unordered_map<int, Bus*> GetBuses() override;
    virtual bool Ok() override;

protected:
    CanAdapter(const std::string& interface_name, const std::string& logger_name = "CanAdapter",
               LogLevel log_level = LogLevel::Info);
    void Stop();
    void Loop();

    virtual void Send(const MotorMessage& message) override;

private:
    int RequestSocketFromBroker() const;

    std::thread loop_thread_;               /**< 接收循环线程 */
    std::unique_ptr<CanHandle> can_handle_; /**< CAN 通信句柄 */
    std::atomic<bool> running_{false};      /**< 运行标志 */
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
    const char* interface_name, const char* logger_name = "CanAdapter",
    int log_level = static_cast<int>(encos::LogLevel::Info));
#endif

#ifdef __cplusplus
}
#endif
