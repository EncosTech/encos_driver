#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace encos {

/** @brief 跨平台事件驱动串口；Start/Close/析构须由调用方串行执行 */
class SerialPort {
public:
    using ReceiveCallback = std::function<void(const std::byte*, std::size_t)>;
    using EventCallback = std::function<void()>;
    /** @brief 打开原生串口或引用已由 JS 授权打开的端口，失败时抛异常 */
    explicit SerialPort(const std::string& path);
    ~SerialPort();
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;
    /** @brief 启动接收，仅允许一次；回调串行执行，数据仅在回调期间有效 */
    void Start(ReceiveCallback receive, EventCallback disconnected = {},
               EventCallback priority_failed = {});
    /** @brief 接受一帧发送，不等待应答、不重发；成功不代表设备已收到 */
    bool Write(const void* buffer, std::size_t size);
    /** @brief 是否仍可收发；异步错误后返回 false */
    bool Ok() const;
    /** @brief 停止回调并关闭；禁止在回调内调用，浏览器底层关闭异步完成 */
    void Close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos
