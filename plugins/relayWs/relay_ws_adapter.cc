#include "relayWs/relay_ws_adapter.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "client/ws_client.h"
#include "platform/delay.h"
#include "platform/sync.h"
#include "relay/relay_frame.h"
#include "relayWs/relay_bounded_queue.h"
#include "relayWs/relay_ws_url.h"
#include "utils/thread_priority.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/eventloop.h>
#else
#include <ixwebsocket/IXHttpClient.h>
#endif

namespace encos {

namespace {

#ifdef __EMSCRIPTEN__
constexpr int kDefaultFrequency = 100;
#else
constexpr int kDefaultFrequency = 500;
#endif

constexpr int kMaxReconnectAttempts = 3;
constexpr auto kReconnectInterval = std::chrono::seconds(2);
constexpr auto kWorkerSleepInterval = std::chrono::milliseconds(1);
constexpr auto kInitialConnectTimeout = std::chrono::seconds(6);
constexpr auto kInitialConnectRetryInterval = std::chrono::milliseconds(100);
constexpr auto kSendReadyTimeout = std::chrono::seconds(2);
constexpr std::size_t kMaxOutgoingMessages = 4096;

std::string PerformHttpGet(const std::string& url) {
#ifndef __EMSCRIPTEN__
    ix::HttpClient http_client;
    ix::HttpRequestArgsPtr args = http_client.createRequest(url, ix::HttpClient::kGet);
    const auto response = http_client.request(args->url, ix::HttpClient::kGet, "", args);
    if (response->statusCode == 200) {
        return response->body;
    }
    return "";
#else
    (void) url;
    return "";
#endif
}

}  // namespace

struct RelayWsAdapter::Impl {
    std::string start_url;
    std::string ws_url;
    std::unique_ptr<RelayWsClient> client;
#ifndef __EMSCRIPTEN__
    std::thread worker_thread;
#else
    int interval_id = -1;
#endif
    std::atomic<bool> running{false};
    std::atomic<bool> ok{false};
    std::vector<RelayQueuedMessage> send_buffer;
    std::unordered_map<int, std::size_t> bus_generations;
    platform::Mutex send_buffer_mutex;
#ifndef __EMSCRIPTEN__
    platform::Mutex connection_mutex;
    std::condition_variable_any connection_cv;
#endif
    int reconnect_attempts = 0;
    std::chrono::steady_clock::time_point last_reconnect_time;
    std::chrono::steady_clock::time_point last_flush_time;
    std::chrono::steady_clock::time_point last_incoming_drop_report;
    std::size_t pending_incoming_drops = 0;
    int frequency = kDefaultFrequency;
    int bus_count = 0;
    std::string session_id;
    std::string release_url;
    bool connected_once = false;
};

RelayWsAdapter::RelayWsAdapter(const std::string& interface_name, const std::string& logger_name,
                               LogLevel log_level)
    : BaseAdapter(interface_name, logger_name.empty() ? "RelayWsAdapter" : logger_name, log_level),
      impl_(std::make_unique<Impl>()) {
    impl_->start_url = interface_name;
    impl_->last_flush_time = std::chrono::steady_clock::now();
    impl_->last_reconnect_time = std::chrono::steady_clock::now();

    impl_->client = std::make_unique<RelayWsClient>();

    impl_->client->SetOnMessage([this](const std::vector<uint8_t>& data) {
        const auto frames = DecodeRelayFrames(data);
        if (!frames.has_value()) {
            return;
        }
        for (const auto& frame : *frames) {
            if (frame.type != RelayFrameType::HelperToRelay) {
                continue;
            }
            OnMessage(frame.records);
        }
    });

    impl_->client->SetOnClose([this]() {
        impl_->ok.store(false);
        impl_->last_reconnect_time = std::chrono::steady_clock::now();
    });

    impl_->running.store(true);

#ifndef __EMSCRIPTEN__
    impl_->connected_once = ConnectSessionWithRetry(kInitialConnectTimeout);
    if (impl_->connected_once) {
        impl_->ok.store(true);
        impl_->last_reconnect_time = std::chrono::steady_clock::now();
    }
    impl_->worker_thread = std::thread([this]() {
        WorkerLoop();
    });
#else
    impl_->interval_id = emscripten_set_interval(&RelayWsAdapter::OnTimer, 10, this);
#endif
}

RelayWsAdapter::~RelayWsAdapter() {
    impl_->running.store(false);
#ifndef __EMSCRIPTEN__
    impl_->connection_cv.notify_all();
#endif

#ifndef __EMSCRIPTEN__
    if (impl_->worker_thread.joinable()) {
        impl_->worker_thread.join();
    }
#else
    if (impl_->interval_id >= 0) {
        emscripten_clear_interval(impl_->interval_id);
        impl_->interval_id = -1;
    }
#endif

    if (!impl_->release_url.empty()) {
        (void) PerformHttpGet(impl_->release_url);
    }

    if (impl_->client) {
        impl_->client->Disconnect();
    }
}

std::unordered_map<int, Bus*> RelayWsAdapter::GetBuses() {
    std::unordered_map<int, Bus*> buses;
    for (int bus_idx = 0; bus_idx < impl_->bus_count; ++bus_idx) {
        buses[bus_idx] = GetBus(bus_idx);
    }
    return buses;
}

bool RelayWsAdapter::Ok() {
    return impl_->ok.load() && impl_->client->IsConnected();
}

void RelayWsAdapter::Send(const MotorMessage& message) {
    if (!impl_->ok.load()) {
        (void) WaitForConnection(kSendReadyTimeout);
    }

    platform::LockGuard<platform::Mutex> lock(impl_->send_buffer_mutex);
    impl_->send_buffer.push_back(
        RelayQueuedMessage{message, impl_->bus_generations[message.bus_idx]});
    TrimRelayMessagesToNewest(impl_->send_buffer, kMaxOutgoingMessages);
}

void RelayWsAdapter::SendSynchronized(const MotorMessages& messages) {
    if (messages.empty()) {
        return;
    }
    if (!impl_->ok.load()) {
        (void) WaitForConnection(kSendReadyTimeout);
    }

    platform::LockGuard<platform::Mutex> lock(impl_->send_buffer_mutex);
    std::unordered_set<int> buses;
    for (const auto& message : messages) {
        buses.insert(message.bus_idx);
    }
    for (const int bus_idx : buses) {
        ++impl_->bus_generations[bus_idx];
    }
    for (const auto& message : messages) {
        impl_->send_buffer.push_back(
            RelayQueuedMessage{message, impl_->bus_generations[message.bus_idx]});
    }
    for (const int bus_idx : buses) {
        ++impl_->bus_generations[bus_idx];
    }
    TrimRelayMessagesToNewest(impl_->send_buffer, kMaxOutgoingMessages);
}

void RelayWsAdapter::WorkerLoop() {
    if (!utils::SetCurrentThreadPriority(50)) {
        Logger()->warn("Failed to set Relay WebSocket loop thread priority");
    }
    while (impl_->running.load()) {
        WorkerLoopIteration();
        std::this_thread::sleep_for(kWorkerSleepInterval);
    }
}

void RelayWsAdapter::WorkerLoopIteration() {
    const auto now = std::chrono::steady_clock::now();

    if (!impl_->connected_once) {
        impl_->connected_once = ConnectSession();
        if (impl_->connected_once) {
            impl_->last_reconnect_time = now;
        }
    }

    const bool is_connected = impl_->client->IsConnected();
    if (is_connected) {
        const bool was_ok = impl_->ok.exchange(true);
        impl_->reconnect_attempts = 0;
        if (!was_ok) {
#ifndef __EMSCRIPTEN__
            impl_->connection_cv.notify_all();
#endif
        }
    } else {
        impl_->ok.store(false);
        if (impl_->connected_once && impl_->reconnect_attempts < kMaxReconnectAttempts) {
            if (now - impl_->last_reconnect_time >= kReconnectInterval) {
                impl_->last_reconnect_time = now;
                ++impl_->reconnect_attempts;
                impl_->client->Connect(impl_->ws_url);
            }
        }
    }

    if (impl_->ok.load()) {
        impl_->client->Poll();
        ReportDroppedIncomingFrames(now);
    }

    if (now - impl_->last_flush_time >= std::chrono::milliseconds(1000 / impl_->frequency)) {
        impl_->last_flush_time = now;
        FlushSendBuffer();
    }
}

void RelayWsAdapter::ReportDroppedIncomingFrames(std::chrono::steady_clock::time_point now) {
    impl_->pending_incoming_drops += impl_->client->TakeDroppedIncomingCount();
    if (impl_->pending_incoming_drops == 0) {
        return;
    }
    if (impl_->last_incoming_drop_report != std::chrono::steady_clock::time_point{} &&
        now - impl_->last_incoming_drop_report < std::chrono::seconds(1)) {
        return;
    }
    Logger()->warn("Relay WebSocket dropped {} incoming frames", impl_->pending_incoming_drops);
    impl_->pending_incoming_drops = 0;
    impl_->last_incoming_drop_report = now;
}

void RelayWsAdapter::OnTimer(void* arg) {
    auto* adapter = static_cast<RelayWsAdapter*>(arg);
    if (adapter != nullptr && adapter->impl_->running.load()) {
        adapter->WorkerLoopIteration();
    }
}

void RelayWsAdapter::FlushSendBuffer() {
    if (!impl_->ok.load()) {
        return;
    }

    std::vector<RelayQueuedMessage> buffer;
    {
        platform::LockGuard<platform::Mutex> lock(impl_->send_buffer_mutex);
        buffer = std::move(impl_->send_buffer);
        impl_->send_buffer.clear();
    }

    const auto bytes = EncodeQueuedRelayFrames(RelayFrameType::RelayToHelper, buffer);
    if (!impl_->client->SendBinary(bytes)) {
        impl_->ok.store(false);
        platform::LockGuard<platform::Mutex> lock(impl_->send_buffer_mutex);
        ReinsertFailedRelayMessages(impl_->send_buffer, std::move(buffer), kMaxOutgoingMessages);
    }
}

bool RelayWsAdapter::ConnectSession() {
    if (impl_->ws_url.empty()) {
        if (impl_->start_url.rfind("ws://", 0) == 0 || impl_->start_url.rfind("wss://", 0) == 0) {
            impl_->ws_url = impl_->start_url;
            const char separator = impl_->ws_url.find('?') == std::string::npos ? '?' : '&';
            impl_->ws_url += separator;
            impl_->ws_url += "freq=";
            impl_->ws_url += std::to_string(impl_->frequency);
        } else {
            const auto parsed = ParseRelayWsStartUrl(impl_->start_url);
            if (!parsed.has_value()) {
                return false;
            }

            const std::string response = PerformHttpGet(impl_->start_url);
            const auto start_response = ParseRelayStartResponse(response);
            if (start_response.session.empty()) {
                return false;
            }

            impl_->bus_count = start_response.bus_count;
            impl_->session_id = start_response.session;

            std::ostringstream release_url;
            release_url << parsed->scheme << "://" << parsed->host << ":" << parsed->port
                        << "/release?session=" << start_response.session << "&" << parsed->query;
            impl_->release_url = release_url.str();

            std::ostringstream ws_url;
            ws_url << "ws://" << parsed->host << ":" << parsed->port
                   << "/ws?session=" << start_response.session << "&freq=" << impl_->frequency;
            impl_->ws_url = ws_url.str();
        }
    }

    if (!impl_->client->Connect(impl_->ws_url)) {
        return false;
    }

#ifndef __EMSCRIPTEN__
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!impl_->client->IsConnected() && std::chrono::steady_clock::now() < deadline) {
        impl_->client->Poll();
        ReportDroppedIncomingFrames(std::chrono::steady_clock::now());
        if (impl_->client->IsClosed()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return impl_->client->IsConnected();
#else
    return true;
#endif
}

bool RelayWsAdapter::ConnectSessionWithRetry(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (impl_->running.load()) {
        if (ConnectSession()) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        platform::SleepFor(kInitialConnectRetryInterval);
    }
    return false;
}

bool RelayWsAdapter::WaitForConnection(std::chrono::milliseconds timeout) {
    if (impl_->ok.load()) {
        return true;
    }

#ifdef __EMSCRIPTEN__
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (impl_->running.load() && !impl_->ok.load() &&
           std::chrono::steady_clock::now() < deadline) {
        platform::SleepFor(std::chrono::milliseconds(2));
    }
    return impl_->ok.load();
#else
    platform::UniqueLock<platform::Mutex> lock(impl_->connection_mutex);
    impl_->connection_cv.wait_for(lock, timeout, [this]() {
        return impl_->ok.load() || !impl_->running.load();
    });
    return impl_->ok.load();
#endif
}

BaseAdapter* CreateRelayWsAdapterStatic(const std::string& interface_name,
                                        const std::string& logger_name, LogLevel log_level) {
    return new RelayWsAdapter(interface_name, logger_name, log_level);
}

}  // namespace encos
