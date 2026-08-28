#include <emscripten/websocket.h>
#include <mutex>
#include <vector>

#include "client/ws_client.h"

namespace encos {

namespace {

constexpr std::size_t kMaxIncomingFrames = 1024;
constexpr std::size_t kMaxIncomingBytes = 8u * 1024u * 1024u;

EM_BOOL HandleOpen(int /*event_type*/, const EmscriptenWebSocketOpenEvent* /*event*/,
                   void* user_data) {
    auto* client = static_cast<RelayWsClient*>(user_data);
    if (client == nullptr) {
        return EM_FALSE;
    }
    return EM_TRUE;
}

EM_BOOL HandleError(int /*event_type*/, const EmscriptenWebSocketErrorEvent* /*event*/,
                    void* user_data) {
    auto* client = static_cast<RelayWsClient*>(user_data);
    if (client == nullptr) {
        return EM_FALSE;
    }
    client->OnClose();
    return EM_TRUE;
}

EM_BOOL HandleClose(int /*event_type*/, const EmscriptenWebSocketCloseEvent* /*event*/,
                    void* user_data) {
    auto* client = static_cast<RelayWsClient*>(user_data);
    if (client == nullptr) {
        return EM_FALSE;
    }
    client->OnClose();
    return EM_TRUE;
}

EM_BOOL HandleMessage(int /*event_type*/, const EmscriptenWebSocketMessageEvent* event,
                      void* user_data) {
    auto* client = static_cast<RelayWsClient*>(user_data);
    if (client == nullptr || event == nullptr || event->isText) {
        return EM_FALSE;
    }
    client->OnMessage(static_cast<const uint8_t*>(event->data), static_cast<int>(event->numBytes));
    return EM_TRUE;
}

}  // namespace

struct RelayWsClient::Impl {
    EMSCRIPTEN_WEBSOCKET_T handle = 0;
    OnMessageCallback on_message_;
    OnCloseCallback on_close_;
    mutable std::mutex mutex_;
    std::vector<std::vector<uint8_t>> incoming_;
    std::size_t incoming_bytes_ = 0;
    std::size_t dropped_incoming_ = 0;
    bool closed_ = false;
    bool close_reported_ = false;
};

RelayWsClient::RelayWsClient() : impl_(std::make_unique<Impl>()) {}

RelayWsClient::~RelayWsClient() {
    Disconnect();
}

bool RelayWsClient::Connect(const std::string& url) {
    Disconnect();

    if (!emscripten_websocket_is_supported()) {
        return false;
    }

    EmscriptenWebSocketCreateAttributes attrs{};
    attrs.url = url.c_str();
    attrs.protocols = nullptr;
    attrs.createOnMainThread = EM_TRUE;

    impl_->handle = emscripten_websocket_new(&attrs);
    if (impl_->handle <= 0) {
        impl_->handle = 0;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->closed_ = false;
        impl_->close_reported_ = false;
        impl_->incoming_.clear();
        impl_->incoming_bytes_ = 0;
    }

    emscripten_websocket_set_onopen_callback(impl_->handle, this, HandleOpen);
    emscripten_websocket_set_onerror_callback(impl_->handle, this, HandleError);
    emscripten_websocket_set_onclose_callback(impl_->handle, this, HandleClose);
    emscripten_websocket_set_onmessage_callback(impl_->handle, this, HandleMessage);
    return true;
}

void RelayWsClient::Disconnect() {
    if (impl_->handle != 0) {
        emscripten_websocket_delete(impl_->handle);
        impl_->handle = 0;
    }
    OnClose();
}

bool RelayWsClient::IsConnected() const {
    if (impl_->handle == 0) {
        return false;
    }
    unsigned short ready_state = 0;
    if (emscripten_websocket_get_ready_state(impl_->handle, &ready_state) !=
        EMSCRIPTEN_RESULT_SUCCESS) {
        return false;
    }
    return ready_state == 1;
}

bool RelayWsClient::IsClosed() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->closed_;
}

bool RelayWsClient::SendBinary(const std::vector<uint8_t>& data) {
    if (impl_->handle == 0 || data.empty()) {
        return false;
    }
    return emscripten_websocket_send_binary(impl_->handle, const_cast<uint8_t*>(data.data()),
                                            static_cast<int>(data.size())) ==
           EMSCRIPTEN_RESULT_SUCCESS;
}

void RelayWsClient::SetOnMessage(OnMessageCallback callback) {
    impl_->on_message_ = std::move(callback);
}

void RelayWsClient::SetOnClose(OnCloseCallback callback) {
    impl_->on_close_ = std::move(callback);
}

void RelayWsClient::Poll() {
    std::vector<std::vector<uint8_t>> messages;
    bool notify_close = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        messages = std::move(impl_->incoming_);
        impl_->incoming_.clear();
        impl_->incoming_bytes_ = 0;
        if (impl_->closed_ && !impl_->close_reported_) {
            impl_->close_reported_ = true;
            notify_close = true;
        }
    }

    if (impl_->on_message_) {
        for (const auto& message : messages) {
            impl_->on_message_(message);
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
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (size > kMaxIncomingBytes) {
        ++impl_->dropped_incoming_;
        return;
    }
    while (!impl_->incoming_.empty() && (impl_->incoming_.size() >= kMaxIncomingFrames ||
                                         impl_->incoming_bytes_ + size > kMaxIncomingBytes)) {
        impl_->incoming_bytes_ -= impl_->incoming_.front().size();
        impl_->incoming_.erase(impl_->incoming_.begin());
        ++impl_->dropped_incoming_;
    }
    impl_->incoming_.emplace_back(data, data + len);
    impl_->incoming_bytes_ += size;
}

void RelayWsClient::OnClose() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->closed_ = true;
}

std::size_t RelayWsClient::TakeDroppedIncomingCount() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    const auto dropped = impl_->dropped_incoming_;
    impl_->dropped_incoming_ = 0;
    return dropped;
}

}  // namespace encos
