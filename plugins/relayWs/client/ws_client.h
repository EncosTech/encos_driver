#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace encos {

/**
 * @brief WebSocket 客户端
 *
 * 同一个头文件在 Native 与 Emscripten 下共用；具体实现分别位于
 * client/ix_ws_client.cc 与 client/emscripten_ws_client.cc。
 * CMake 按平台选择编译其中一份源文件。
 */
class RelayWsClient {
public:
    using OnMessageCallback = std::function<void(const std::vector<uint8_t>&)>;
    using OnCloseCallback = std::function<void()>;

    RelayWsClient();
    ~RelayWsClient();

    /**
     * @brief 连接到指定 WebSocket URL
     * @param url ws://host:port/path?query 形式
     * @return 是否成功开始连接
     */
    bool Connect(const std::string& url);

    /**
     * @brief 断开连接
     */
    void Disconnect();

    /**
     * @brief 检查是否已连接
     */
    bool IsConnected() const;

    /**
     * @brief 检查连接是否已经关闭或连接失败
     */
    bool IsClosed() const;

    /**
     * @brief 发送二进制数据
     * @param data 待发送字节
     * @return 是否成功入队/发送
     */
    bool SendBinary(const std::vector<uint8_t>& data);

    /**
     * @brief 注册收到二进制消息回调
     */
    void SetOnMessage(OnMessageCallback callback);

    /**
     * @brief 注册连接关闭回调
     */
    void SetOnClose(OnCloseCallback callback);

    /**
     * @brief 轮询/处理连接事件（对需要显式轮询的实现）
     */
    void Poll();

    /** @brief 读取并清零最近一次轮询前丢弃的入站帧数量。 */
    std::size_t TakeDroppedIncomingCount();

    // Emscripten 回调内部入口，不应被业务代码直接调用
    void OnMessage(const uint8_t* data, int len);
    void OnClose();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos
