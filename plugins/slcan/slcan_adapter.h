#pragma once

#include <array>
#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>

#include "adapter/base_adapter.h"
#include "export.h"
#include "platform/sync.h"
#include "serialib.h"

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

    SlcanAdapter(const SlcanAdapter&) = delete;
    SlcanAdapter& operator=(const SlcanAdapter&) = delete;
    virtual ~SlcanAdapter() override;

    virtual std::unordered_map<int, Bus*> GetBuses() override;
    virtual bool Ok() override;

protected:
    SlcanAdapter(const std::string& interface_name, const std::string& logger_name = "SlcanAdapter",
                 LogLevel log_level = LogLevel::Info);

    virtual void Send(const MotorMessage& message) override;

private:
    std::thread loop_thread_;                /**< 接收循环线程 */
    platform::Mutex serial_mutex_;           /**< 串口访问互斥锁 */
    std::shared_ptr<serialib> serial_port_;  /**< 串口句柄 */
    std::array<char, 1024> read_buffer_;     /**< 读取缓冲区 */
    std::atomic<uint16_t> read_buf_size_{0}; /**< 缓冲区数据大小 */
    std::atomic<bool> loop_stopping_{false}; /**< 接收循环停止标志 */

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
    const char* interface_name, const char* logger_name = "SlcanAdapter",
    int log_level = static_cast<int>(encos::LogLevel::Info));
#endif

#ifdef __cplusplus
}
#endif
