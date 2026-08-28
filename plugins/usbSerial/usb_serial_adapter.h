#pragma once

#include <array>
#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "adapter/base_adapter.h"
#include "export.h"
#include "platform/sync.h"
#include "serialib.h"
#include "usb_serial_sender.h"

namespace encos {

/**
 * @brief USB 串行适配器
 *
 * 通过 USB-CAN 转换器的二进制协议实现 CAN 通信。
 * 适用于使用自定义二进制协议的 USB-CAN 设备。
 */
class UsbSerialAdapter : public BaseAdapter {
public:
    /**
     * @brief 创建 USB 串行适配器实例
     * @param interface_name 串口设备路径（如 "/dev/tty_usb0"）
     * @param logger_name 日志记录器名称
     * @param log_level 日志级别
     * @return 适配器指针
     */
    static UsbSerialAdapter* Create(const std::string& interface_name,
                                    const std::string& logger_name = "UsbSerialAdapter",
                                    LogLevel log_level = LogLevel::Info);

    UsbSerialAdapter(const UsbSerialAdapter&) = delete;
    UsbSerialAdapter& operator=(const UsbSerialAdapter&) = delete;
    virtual ~UsbSerialAdapter() override;

    virtual std::unordered_map<int, Bus*> GetBuses() override;
    virtual bool Ok() override;

protected:
    UsbSerialAdapter(const std::string& interface_name,
                     const std::string& logger_name = "UsbSerialAdapter",
                     LogLevel log_level = LogLevel::Info);

    virtual void Send(const MotorMessage& message) override;

private:
    std::thread loop_thread_;                 /**< 接收循环线程 */
    platform::Mutex serial_mutex_;            /**< 串口访问互斥锁 */
    std::shared_ptr<serialib> serial_port_;   /**< 串口句柄 */
    std::array<std::byte, 1024> read_buffer_; /**< 读取缓冲区 */
    std::atomic<uint16_t> read_buf_size_{0};  /**< 缓冲区数据大小 */
    std::atomic<bool> running_{false};        /**< 接收循环运行标志 */
    std::unique_ptr<UsbSerialSender> sender_; /**< 同步首发与有界重发调度器 */
    static constexpr std::size_t kMaxPendingRetries = 256;

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
    const char* interface_name, const char* logger_name = "UsbSerialAdapter",
    int log_level = static_cast<int>(encos::LogLevel::Info));
#endif

#ifdef __cplusplus
}
#endif
