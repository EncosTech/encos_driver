// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstddef>
#include <memory>
#include <string>
namespace encos {
/** @brief 原生串口系统调用封装；由 SerialPort 管理并发和生命周期 */
class SerialHandle {
public:
    explicit SerialHandle(const std::string& path);
    ~SerialHandle();
    SerialHandle(const SerialHandle&) = delete;
    SerialHandle& operator=(const SerialHandle&) = delete;
    /** @brief 等待可用数据，返回字节数；取消返回 0，错误返回 -1 */
    int Read(void* buffer, std::size_t capacity);
    /** @brief 续写完整帧，背压预算 100 ms，不重发；调用方串行化写入 */
    bool Write(const void* buffer, std::size_t size);
    /** @brief 永久取消读写等待；销毁前须等待所有访问结束 */
    void Cancel() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace encos
