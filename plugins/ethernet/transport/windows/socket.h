#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Winsock must precede Windows/IP Helper headers.
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
// clang-format on
#include <memory>
#include <string>
#include <vector>
namespace encos::ethernet {
class DatagramChannel;
struct Endpoint;
/** @brief Winsock进程生命周期，使用套接字前调用。 */
void InitializeWinsock();
/** @brief 保存网卡身份和IPv4地址，名称与平台枚举保持一致。 */
struct WindowsInterface {
    std::string name;
    unsigned index = 0;
    in_addr address{};
    unsigned prefix_length = 0;
    bool dhcp = false;
    bool up = false;
};
/** @brief 查询Windows IPv4网卡；配置阶段可包含禁用或尚未取得地址的网卡。 */
std::vector<WindowsInterface> WindowsInterfaces(bool include_unconfigured = false);
/** @brief 独占所有权的非阻塞Winsock套接字。 */
class WindowsSocket {
public:
    WindowsSocket();
    ~WindowsSocket();
    WindowsSocket(const WindowsSocket&) = delete;
    WindowsSocket& operator=(const WindowsSocket&) = delete;
    SOCKET Get() const {
        return socket_;
    }
    void Bind(const WindowsInterface& iface);

private:
    SOCKET socket_ = INVALID_SOCKET;
};
/** @brief 创建已连接的数据套接字，并把可读通知关联到共享事件。 */
std::unique_ptr<DatagramChannel> ConnectWindowsDatagram(const Endpoint& endpoint, WSAEVENT event);
/** @brief 带Winsock错误号的异常。 */
[[noreturn]] void SocketError(const char* operation);
}  // namespace encos::ethernet
