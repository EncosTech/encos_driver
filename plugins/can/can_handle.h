#pragma once

#include <atomic>
#include <functional>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "bus/bus.h"

namespace encos {

/**
 * @brief SocketCAN 直接通信句柄
 *
 * 封装 Linux SocketCAN 接口的底层通信，适用于无需守护进程的直接通信场景。
 * 仅在 Linux 平台上可用。
 */
class CanHandle {
public:
    /**
     * @brief 构造 CAN 句柄
     * @param interface_name CAN 接口名称（如 "can0", "vcan0"）
     */
    CanHandle(const std::string& interface_name);
    explicit CanHandle(int existing_fd);
    ~CanHandle();

    /**
     * @brief 发送电机消息
     * @param message 电机消息
     */
    void Send(const MotorMessage& message);

    /**
     * @brief 启动接收循环
     *
     * 在独立线程中持续接收 CAN 消息，并通过回调函数传递给上层
     */
    void Loop();

    /**
     * @brief 设置消息接收回调
     * @param callback 回调函数
     */
    void SetCallback(const std::function<void(MotorMessage)>& callback);

    /**
     * @brief 停止接收循环
     */
    void Stop();

    /**
     * @brief 检查句柄是否正常
     * @return 如果句柄正常返回 true
     */
    bool Ok();

private:
    int can_fd_;                                 /**< CAN 套接字文件描述符 */
    bool fd_frames_enabled_{false};              /**< SocketCAN FD 帧支持 */
    std::atomic<bool> running_;                  /**< 运行标志 */
    std::function<void(MotorMessage)> callback_; /**< 接收回调 */
};

}  // namespace encos
