#include "ethernet/protocol/gateway_payload.h"
// clang-format off
#include "socket.h"
// clang-format on
#include <map>
#include <stdexcept>

#include "ethernet/ethernet_slave.h"
#include "ethernet/transport/datagram.h"
#include "ethernet/transport/transport.h"
namespace encos::ethernet {
namespace {
struct SocketEvent {
    WSAEVENT handle = WSACreateEvent();
    SocketEvent() {
        if (handle == WSA_INVALID_EVENT)
            SocketError("WSACreateEvent");
    }
    ~SocketEvent() {
        WSACloseEvent(handle);
    }
    SocketEvent(const SocketEvent&) = delete;
    SocketEvent& operator=(const SocketEvent&) = delete;
};
class UdpTransport final : public Transport {
    SocketEvent readable_, wake_;
    std::map<unsigned, std::unique_ptr<DatagramChannel>> peers_;
    bool multi_;

public:
    UdpTransport(const std::vector<Endpoint>& endpoints, bool multi) : multi_(multi) {
        for (const auto& endpoint : endpoints) {
            const auto id = multi ? SlaveId(endpoint.address) : 0;
            if (!peers_.emplace(id, ConnectWindowsDatagram(endpoint, readable_.handle)).second)
                throw std::invalid_argument("Duplicate Ethernet peer");
        }
        if (peers_.empty())
            throw std::invalid_argument("No Ethernet peers");
    }
    bool Send(unsigned slave, const MotorMessages& messages) override {
        const auto payload = EncodePayload(messages);
        return peers_.at(slave)->Send(payload.data(), payload.size());
    }
    MotorMessages Receive() override {
        WSAResetEvent(readable_.handle);
        MotorMessages result;
        for (const auto& peer : peers_) {
            // Bound each peer's work so continuous traffic cannot starve other slaves or TX.
            for (unsigned batch = 0; batch < 64; ++batch) {
                std::array<uint8_t, 1449> packet{};
                const auto size = peer.second->Receive(packet.data(), packet.size());
                if (!size)
                    break;
                auto messages = DecodePayload(packet.data(), size);
                if (!messages)
                    continue;
                for (auto& message : *messages) {
                    if (multi_)
                        message.bus_idx |= static_cast<int>(peer.first << 16);
                    result.push_back(message);
                }
            }
        }
        return result;
    }
    void Wait(int milliseconds) override {
        const WSAEVENT events[] = {readable_.handle, wake_.handle};
        const auto result =
            WSAWaitForMultipleEvents(2, events, FALSE, static_cast<DWORD>(milliseconds), FALSE);
        if (result == WSA_WAIT_FAILED)
            SocketError("WSAWaitForMultipleEvents");
        if (result == WSA_WAIT_EVENT_0 + 1)
            WSAResetEvent(wake_.handle);
    }
    void Notify() override {
        WSASetEvent(wake_.handle);
    }
};
}  // namespace
std::unique_ptr<Transport> CreateTransport(const std::vector<Endpoint>& endpoints, bool multi) {
    InitializeWinsock();
    return std::make_unique<UdpTransport>(endpoints, multi);
}
}  // namespace encos::ethernet
