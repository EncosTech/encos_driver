// clang-format off
#include "socket.h"
// clang-format on
#include "ethernet/transport/datagram.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <mswsock.h>
#include <stdexcept>
namespace encos::ethernet {
namespace {
struct WinsockLifetime {
    WinsockLifetime() {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0)
            throw std::runtime_error("WSAStartup failed: " + std::to_string(result));
    }
    ~WinsockLifetime() {
        WSACleanup();
    }
};
std::string ToUtf8(const wchar_t* text) {
    if (!text || !*text)
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        throw std::runtime_error("Invalid Windows iface name");
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}
bool RetryableDatagramError(int error) {
    return error == WSAEWOULDBLOCK || error == WSAEINTR || error == WSAENOBUFS ||
           error == WSAECONNREFUSED || error == WSAECONNRESET || error == WSAENETUNREACH ||
           error == WSAEHOSTUNREACH || error == WSAENETDOWN || error == WSAENETRESET ||
           error == WSAETIMEDOUT;
}
bool SendResult(int result, std::size_t size) {
    if (result == static_cast<int>(size))
        return true;
    const int error = WSAGetLastError();
    if (result == SOCKET_ERROR && RetryableDatagramError(error))
        return false;
    SocketError("send");
}
class ConnectedSocket final : public DatagramChannel {
    WindowsSocket socket_;

public:
    explicit ConnectedSocket(const Endpoint& endpoint, WSAEVENT event = WSA_INVALID_EVENT) {
        const auto interfaces = WindowsInterfaces();
        const auto selected =
            std::find_if(interfaces.begin(), interfaces.end(), [&](const auto& item) {
                return item.name == endpoint.interface_name;
            });
        if (selected == interfaces.end())
            throw std::invalid_argument("No active IPv4 iface: " + endpoint.interface_name);
        socket_.Bind(*selected);
        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(endpoint.port);
        if (inet_pton(AF_INET, endpoint.address.c_str(), &remote.sin_addr) != 1)
            throw std::invalid_argument("Invalid UDP peer address");
        if (connect(socket_.Get(), reinterpret_cast<sockaddr*>(&remote), sizeof remote) != 0)
            SocketError("connect");
        if (event != WSA_INVALID_EVENT && WSAEventSelect(socket_.Get(), event, FD_READ) != 0)
            SocketError("WSAEventSelect");
    }
    bool Send(const uint8_t* data, std::size_t size) override {
        return SendResult(
            send(socket_.Get(), reinterpret_cast<const char*>(data), static_cast<int>(size), 0),
            size);
    }
    std::size_t Receive(uint8_t* data, std::size_t capacity) override {
        const auto size =
            recv(socket_.Get(), reinterpret_cast<char*>(data), static_cast<int>(capacity), 0);
        if (size >= 0)
            return static_cast<std::size_t>(size);
        const int error = WSAGetLastError();
        if (RetryableDatagramError(error) || error == WSAEMSGSIZE)
            return 0;
        SocketError("recv");
    }
    bool Wait(int milliseconds) override {
        WSAPOLLFD wait{socket_.Get(), POLLRDNORM, 0};
        const auto result = WSAPoll(&wait, 1, milliseconds);
        if (result == SOCKET_ERROR)
            SocketError("WSAPoll");
        return result > 0;
    }
};
class BroadcastSocket final : public DiscoveryChannel {
    struct Binding {
        WindowsInterface iface;
        std::unique_ptr<WindowsSocket> socket;
    };
    std::vector<Binding> bindings_;
    LPFN_WSASENDMSG send_message_ = nullptr;

public:
    explicit BroadcastSocket(const std::string& name) {
        for (const auto& iface : WindowsInterfaces()) {
            if (!name.empty() && name != iface.name)
                continue;
            auto socket = std::make_unique<WindowsSocket>();
            socket->Bind(iface);
            const BOOL enabled = TRUE;
            if (setsockopt(socket->Get(), SOL_SOCKET, SO_BROADCAST,
                           reinterpret_cast<const char*>(&enabled), sizeof enabled) != 0)
                SocketError("SO_BROADCAST");
            bindings_.push_back({iface, std::move(socket)});
        }
        if (bindings_.empty())
            throw std::invalid_argument("No active IPv4 broadcast iface: " + name);
        GUID id = WSAID_WSASENDMSG;
        DWORD bytes = 0;
        if (WSAIoctl(bindings_.front().socket->Get(), SIO_GET_EXTENSION_FUNCTION_POINTER, &id,
                     sizeof id, &send_message_, sizeof send_message_, &bytes, nullptr,
                     nullptr) != 0)
            SocketError("WSASendMsg lookup");
    }
    void Broadcast(const uint8_t* data, std::size_t size) override {
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(5001);
        target.sin_addr.s_addr = INADDR_BROADCAST;
        for (const auto& binding : bindings_) {
            WSABUF buffer{static_cast<ULONG>(size),
                          reinterpret_cast<char*>(const_cast<uint8_t*>(data))};
            alignas(WSACMSGHDR) char control[WSA_CMSG_SPACE(sizeof(IN_PKTINFO))]{};
            WSAMSG message{};
            message.name = reinterpret_cast<sockaddr*>(&target);
            message.namelen = sizeof target;
            message.lpBuffers = &buffer;
            message.dwBufferCount = 1;
            message.Control = {sizeof control, control};
            auto* header = WSA_CMSG_FIRSTHDR(&message);
            header->cmsg_level = IPPROTO_IP;
            header->cmsg_type = IP_PKTINFO;
            header->cmsg_len = WSA_CMSG_LEN(sizeof(IN_PKTINFO));
            auto* info = reinterpret_cast<IN_PKTINFO*>(WSA_CMSG_DATA(header));
            info->ipi_ifindex = binding.iface.index;
            info->ipi_addr = binding.iface.address;
            DWORD sent = 0;
            const auto result =
                send_message_(binding.socket->Get(), &message, 0, &sent, nullptr, nullptr);
            (void) SendResult(result == 0 ? static_cast<int>(sent) : SOCKET_ERROR, size);
        }
    }
    DiscoveryDatagram Receive(int milliseconds) override {
        std::vector<WSAPOLLFD> waits;
        for (const auto& binding : bindings_)
            waits.push_back({binding.socket->Get(), POLLRDNORM, 0});
        const auto result = WSAPoll(waits.data(), static_cast<ULONG>(waits.size()), milliseconds);
        if (result == SOCKET_ERROR)
            SocketError("discovery poll");
        for (std::size_t i = 0; i < waits.size(); ++i) {
            if (!(waits[i].revents & POLLRDNORM))
                continue;
            std::array<uint8_t, 65> packet{};
            sockaddr_in source{};
            int length = sizeof source;
            const int received = recvfrom(waits[i].fd, reinterpret_cast<char*>(packet.data()),
                                          static_cast<int>(packet.size()), 0,
                                          reinterpret_cast<sockaddr*>(&source), &length);
            if (received == SOCKET_ERROR) {
                const int error = WSAGetLastError();
                if (RetryableDatagramError(error) || error == WSAEMSGSIZE)
                    continue;
                SocketError("discovery recvfrom");
            }
            char address[INET_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET, &source.sin_addr, address, sizeof address))
                continue;
            return {bindings_[i].iface.name,
                    address,
                    ntohs(source.sin_port),
                    {packet.begin(), packet.begin() + received}};
        }
        return {};
    }
};
}  // namespace
void InitializeWinsock() {
    static WinsockLifetime lifetime;
}
[[noreturn]] void SocketError(const char* operation) {
    throw std::runtime_error(std::string(operation) + " failed: Winsock " +
                             std::to_string(WSAGetLastError()));
}
std::vector<WindowsInterface> WindowsInterfaces(bool include_unconfigured) {
    InitializeWinsock();
    ULONG size = 16384;
    std::vector<uint8_t> buffer(size);
    ULONG status = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        buffer.resize(size);
        status = GetAdaptersAddresses(
            AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
        if (status != ERROR_BUFFER_OVERFLOW)
            break;
    }
    if (status != NO_ERROR)
        throw std::runtime_error("GetAdaptersAddresses failed: " + std::to_string(status));
    std::vector<WindowsInterface> result;
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter;
         adapter = adapter->Next) {
        if ((!include_unconfigured && adapter->OperStatus != IfOperStatusUp) ||
            adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || adapter->IfType == IF_TYPE_IEEE80211)
            continue;
        auto name = ToUtf8(adapter->FriendlyName);
        if (name.empty() && adapter->AdapterName)
            name = adapter->AdapterName;
        const auto before = result.size();
        for (auto* address = adapter->FirstUnicastAddress; address; address = address->Next) {
            if (address->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            result.push_back({name, adapter->IfIndex,
                              reinterpret_cast<sockaddr_in*>(address->Address.lpSockaddr)->sin_addr,
                              address->OnLinkPrefixLength,
                              (adapter->Flags & IP_ADAPTER_DHCP_ENABLED) != 0,
                              adapter->OperStatus == IfOperStatusUp});
        }
        if (include_unconfigured && result.size() == before)
            result.push_back({name,
                              adapter->IfIndex,
                              {},
                              0,
                              (adapter->Flags & IP_ADAPTER_DHCP_ENABLED) != 0,
                              adapter->OperStatus == IfOperStatusUp});
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return (ntohl(a.address.s_addr) == 0xc0a864feU) > (ntohl(b.address.s_addr) == 0xc0a864feU);
    });
    return result;
}
WindowsSocket::WindowsSocket() {
    InitializeWinsock();
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET)
        SocketError("socket");
    u_long enabled = 1;
    if (ioctlsocket(socket_, FIONBIO, &enabled) != 0) {
        closesocket(socket_);
        SocketError("FIONBIO");
    }
}
WindowsSocket::~WindowsSocket() {
    closesocket(socket_);
}
void WindowsSocket::Bind(const WindowsInterface& iface) {
    const auto index = htonl(iface.index);
    if (setsockopt(socket_, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&index),
                   sizeof index) != 0)
        SocketError("IP_UNICAST_IF");
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr = iface.address;
    if (bind(socket_, reinterpret_cast<sockaddr*>(&local), sizeof local) != 0)
        SocketError("bind");
}
std::unique_ptr<DatagramChannel> ConnectDatagram(const Endpoint& endpoint) {
    return std::make_unique<ConnectedSocket>(endpoint);
}
std::unique_ptr<DiscoveryChannel> OpenDiscovery(const std::string& name) {
    return std::make_unique<BroadcastSocket>(name);
}
std::unique_ptr<DatagramChannel> ConnectWindowsDatagram(const Endpoint& endpoint, WSAEVENT event) {
    return std::make_unique<ConnectedSocket>(endpoint, event);
}
bool SetManagementThreadScheduling() {
    return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL) != 0;
}
}  // namespace encos::ethernet
