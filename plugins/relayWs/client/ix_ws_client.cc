#include <condition_variable>
#include <deque>
#include <ixwebsocket/IXWebSocket.h>
#include <mutex>

#include "client/ws_client.h"
#include "platform/sync.h"

namespace encos {

namespace {
constexpr std::size_t kMaxIncomingFrames = 1024;
constexpr std::size_t kMaxIncomingBytes = 8u * 1024u * 1024u;
}  // namespace

struct RelayWsClient::Impl {
    ix::WebSocket ws_;
    mutable platform::Mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<std::string> incoming_;
    std::size_t incoming_bytes_ = 0;
    std::size_t dropped_incoming_ = 0;
    bool closed_ = false;
    bool close_reported_ = false;
    OnMessageCallback on_message_;
    OnCloseCallback on_close_;
};

RelayWsClient::RelayWsClient() : impl_(std::make_unique<Impl>()) {
    impl_->ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
        if (message->type == ix::WebSocketMessageType::Message && message->binary) {
            OnMessage(reinterpret_cast<const uint8_t*>(message->str.data()),
                      static_cast<int>(message->str.size()));
        } else if (message->type == ix::WebSocketMessageType::Close ||
                   message->type == ix::WebSocketMessageType::Error) {
            OnClose();
        }
        impl_->cv_.notify_all();
    });
}

RelayWsClient::~RelayWsClient() {
    Disconnect();
}

bool RelayWsClient::Connect(const std::string& url) {
    impl_->ws_.stop();
    {
        platform::LockGuard<platform::Mutex> lock(impl_->mutex_);
        std::deque<std::string> empty;
        impl_->incoming_.swap(empty);
        impl_->incoming_bytes_ = 0;
        impl_->closed_ = false;
        impl_->close_reported_ = false;
    }
    impl_->ws_.setUrl(url);
    impl_->ws_.disableAutomaticReconnection();
    impl_->ws_.start();
    return true;
}

void RelayWsClient::Disconnect() {
    impl_->ws_.stop();
    {
        platform::LockGuard<platform::Mutex> lock(impl_->mutex_);
        impl_->closed_ = true;
    }
    impl_->cv_.notify_all();
}

bool RelayWsClient::IsConnected() const {
    return impl_->ws_.getReadyState() == ix::ReadyState::Open;
}

bool RelayWsClient::IsClosed() const {
    platform::LockGuard<platform::Mutex> lock(impl_->mutex_);
    return impl_->closed_;
}

bool RelayWsClient::SendBinary(const std::vector<uint8_t>& data) {
    if (impl_->ws_.getReadyState() != ix::ReadyState::Open) {
        return false;
    }
    const std::string payload(reinterpret_cast<const char*>(data.data()), data.size());
    return impl_->ws_.sendBinary(payload).success;
}

void RelayWsClient::SetOnMessage(OnMessageCallback callback) {
    impl_->on_message_ = std::move(callback);
}

void RelayWsClient::SetOnClose(OnCloseCallback callback) {
    impl_->on_close_ = std::move(callback);
}

void RelayWsClient::Poll() {
    std::vector<std::string> messages;
    bool notify_close = false;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->mutex_);
        messages.reserve(impl_->incoming_.size());
        while (!impl_->incoming_.empty()) {
            messages.push_back(std::move(impl_->incoming_.front()));
            impl_->incoming_.pop_front();
        }
        impl_->incoming_bytes_ = 0;
        if (impl_->closed_ && !impl_->close_reported_) {
            impl_->close_reported_ = true;
            notify_close = true;
        }
    }

    if (impl_->on_message_) {
        for (const auto& message : messages) {
            const std::vector<uint8_t> data(message.begin(), message.end());
            impl_->on_message_(data);
        }
    }

    if (notify_close && impl_->on_close_) {
        impl_->on_close_();
    }
}

void RelayWsClient::OnMessage(const uint8_t* data, int len) {
    if (data == nullptr || len <= 0) {
        return;
    }
    const auto size = static_cast<std::size_t>(len);
    platform::LockGuard<platform::Mutex> lock(impl_->mutex_);
    if (size > kMaxIncomingBytes) {
        ++impl_->dropped_incoming_;
        return;
    }
    while (!impl_->incoming_.empty() && (impl_->incoming_.size() >= kMaxIncomingFrames ||
                                         impl_->incoming_bytes_ + size > kMaxIncomingBytes)) {
        impl_->incoming_bytes_ -= impl_->incoming_.front().size();
        impl_->incoming_.pop_front();
        ++impl_->dropped_incoming_;
    }
    impl_->incoming_.emplace_back(reinterpret_cast<const char*>(data), size);
    impl_->incoming_bytes_ += size;
}

void RelayWsClient::OnClose() {
    platform::LockGuard<platform::Mutex> lock(impl_->mutex_);
    impl_->closed_ = true;
}

std::size_t RelayWsClient::TakeDroppedIncomingCount() {
    platform::LockGuard<platform::Mutex> lock(impl_->mutex_);
    const auto dropped = impl_->dropped_incoming_;
    impl_->dropped_incoming_ = 0;
    return dropped;
}

}  // namespace encos
