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
 * 仅在 Linux 平台上可用。销毁前调用方必须停止并等待收发线程退出。
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
    CanHandle(const CanHandle&) = delete;
    CanHandle& operator=(const CanHandle&) = delete;

    /**
     * @brief 非阻塞发送独立报文，不等待应答；背压或错误时记录失败，不重发
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
     * @brief 在启动 Loop 前设置单帧回调，替代批量回调
     * @param callback 回调函数
     */
    void SetCallback(const std::function<void(MotorMessage)>& callback);

    /** @brief 设置批量回调，替代单帧回调；回调只能在启动 Loop 前配置 */
    void SetBatchCallback(const std::function<void(const MotorMessages&)>& callback);

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
    void InitializeWakeFd();
    int can_fd_{-1};                             /**< CAN 套接字文件描述符 */
    int wake_fd_{-1};                            /**< 停止事件描述符 */
    bool fd_frames_enabled_{false};              /**< SocketCAN FD 帧支持 */
    std::atomic<bool> running_{false};           /**< 运行标志 */
    std::function<void(MotorMessage)> callback_; /**< 接收回调 */
    std::function<void(const MotorMessages&)> batch_callback_;
};

}  // namespace encos
