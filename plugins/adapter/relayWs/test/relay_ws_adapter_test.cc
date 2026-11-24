// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "relayWs/relay_ws_adapter.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <gtest/gtest.h>
#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "bus/bus.h"
#include "client/ws_client.h"
#include "device/motor/motor.h"
#include "driver_manager_test_access.h"
#include "managed_adapter_test.h"
#include "relayWs/relay_bounded_queue.h"
#include "relayWs/relay_wire_state.h"
#include "utils/emr1/relay_frame.h"
#include "wait_observer.h"

namespace encos {

namespace {

constexpr float kExpectedPositionRad = 0.5f;
constexpr float kExpectedPositionDeg = kExpectedPositionRad * 180.0f / 3.14159265358979323846f;

void WriteFloatBe(float value, uint8_t* out) {
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    out[0] = static_cast<uint8_t>((raw >> 24) & 0xFF);
    out[1] = static_cast<uint8_t>((raw >> 16) & 0xFF);
    out[2] = static_cast<uint8_t>((raw >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(raw & 0xFF);
}

class RelayParameterServer {
public:
    explicit RelayParameterServer(int port, bool emr2 = false) : port_(port), emr2_(emr2) {
        ix::initNetSystem();
    }

    ~RelayParameterServer() {
        Stop();
    }

    bool Start() {
        server_ = std::make_unique<ix::WebSocketServer>(port_, "127.0.0.1");
        server_->disablePerMessageDeflate();
        server_->setOnConnectionCallback(
            [this](std::weak_ptr<ix::WebSocket> weak_socket,
                   std::shared_ptr<ix::ConnectionState> /*connection_state*/) {
                auto socket = weak_socket.lock();
                if (!socket) {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(socket_mutex_);
                    socket_ = socket;
                }
                socket->setOnMessageCallback([this, socket](const ix::WebSocketMessagePtr& msg) {
                    if (msg->type != ix::WebSocketMessageType::Message || !msg->binary) {
                        return;
                    }

                    const std::vector<uint8_t> data(msg->str.begin(), msg->str.end());
                    if (auto capability = DecodeRelayCapability(data)) {
                        if (emr2_) {
                            capability->response = true;
                            capability->capabilities.emr2 = true;
                            capability->capabilities.max_payload = 128;
                            const auto bytes = EncodeRelayCapability(*capability);
                            socket->sendBinary(std::string(
                                reinterpret_cast<const char*>(bytes.data()), bytes.size()));
                        }
                        return;
                    }
                    const auto frames = DecodeRelayPackets(data);
                    if (!frames.has_value()) {
                        return;
                    }

                    for (const auto& frame : *frames) {
                        if (frame.type != RelayFrameType::RelayToHelper) {
                            continue;
                        }
                        if (frame.records.empty()) {
                            empty_frame_count_.fetch_add(1);
                        }
                        if (frame.protocol == RelayProtocol::Emr2) {
                            emr2_packets_.fetch_add(1);
                        }
                        for (const auto& record : frame.records) {
                            const auto& request = record.message;
                            if (request.data.len == 64) {
                                large_records_.fetch_add(1);
                                if (data.size() > 128) {
                                    oversized_packets_.fetch_add(1);
                                }
                            }
                            if (!IsPositionRead(request)) {
                                continue;
                            }

                            request_count_.fetch_add(1);
                            MotorMessage reply{};
                            reply.bus_idx = request.bus_idx;
                            reply.data.id = request.data.id;
                            reply.data.len = 6;
                            reply.data.data[0] = static_cast<uint8_t>(0x05 << 5);
                            reply.data.data[1] = static_cast<uint8_t>(MotorParameter::Position);
                            WriteFloatBe(kExpectedPositionDeg, reply.data.data + 2);

                            RelayWireOptions options;
                            options.protocol = frame.protocol;
                            const auto bytes = EncodeRelayPackets(RelayFrameType::HelperToRelay,
                                                                  MotorMessages{reply}, options)
                                                   .front();
                            socket->sendBinary(std::string(
                                reinterpret_cast<const char*>(bytes.data()), bytes.size()));
                        }
                    }
                });
            });

        const auto listen = server_->listen();
        if (!listen.first) {
            last_error_ = listen.second;
            return false;
        }
        server_->start();
        return true;
    }

    void Stop() {
        if (server_) {
            server_->stop();
            server_.reset();
        }
    }

    int RequestCount() const {
        return request_count_.load();
    }

    int EmptyFrameCount() const {
        return empty_frame_count_.load();
    }

    bool SendRecords(const MotorMessages& messages) {
        std::shared_ptr<ix::WebSocket> socket;
        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            socket = socket_.lock();
        }
        if (!socket) {
            return false;
        }
        const auto bytes = EncodeRelayFrames(RelayFrameType::HelperToRelay, messages);
        socket->sendBinary(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        return true;
    }

    int LargeRecords() const {
        return large_records_.load();
    }
    int OversizedPackets() const {
        return oversized_packets_.load();
    }
    int Emr2Packets() const {
        return emr2_packets_.load();
    }

    const std::string& LastError() const {
        return last_error_;
    }

private:
    static bool IsPositionRead(const MotorMessage& message) {
        return message.data.len == 2 && message.data.data[0] == static_cast<uint8_t>(0x07 << 5) &&
               message.data.data[1] == static_cast<uint8_t>(MotorParameter::Position);
    }

    int port_;
    bool emr2_;
    std::atomic<int> large_records_{0};
    std::atomic<int> oversized_packets_{0};
    std::atomic<int> emr2_packets_{0};
    std::unique_ptr<ix::WebSocketServer> server_;
    std::atomic<int> request_count_{0};
    std::atomic<int> empty_frame_count_{0};
    std::string last_error_;
    std::mutex socket_mutex_;
    std::weak_ptr<ix::WebSocket> socket_;
};

class TestRelayWsAdapter : public RelayWsAdapter {
public:
    using RelayWsAdapter::RelayWsAdapter;
    using RelayWsAdapter::SendSynchronized;
};

}  // namespace

TEST(RelayWsAdapterTests, ConstructorWaitsForDelayedWebSocketConnection) {
    const int port = ix::getFreePort();
    RelayParameterServer server(port);

    auto adapter_future = std::async(std::launch::async, [port]() {
        const auto interface_name = "ws://127.0.0.1:" + std::to_string(port) + "/ws";
        return EncosDriverManager::Instance().CreateAdapterWithFactory(
            interface_name, [interface_name]() {
                return CreateRelayWsAdapterStatic(interface_name, "RelayWsDelayedConnectTest",
                                                  LogLevel::Info);
            });
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_EQ(adapter_future.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);

    ASSERT_TRUE(server.Start()) << server.LastError();
    ASSERT_EQ(adapter_future.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    ManagedAdapterGuard<BaseAdapter> adapter(adapter_future.get());
    ASSERT_TRUE(adapter->Ok());

    auto motor = adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2);

    const auto position = motor->GetParameter<MotorParameter::Position>();
    EXPECT_NEAR(position, kExpectedPositionRad, 0.001f);
    EXPECT_GE(server.RequestCount(), 1);
}

TEST(RelayWsAdapterTests, SendsEmptyFrameAtFixedPeriodWhenIdle) {
    const int port = ix::getFreePort();
    RelayParameterServer server(port);
    ASSERT_TRUE(server.Start()) << server.LastError();

    const auto interface_name = "ws://127.0.0.1:" + std::to_string(port) + "/ws";
    ManagedAdapterGuard<BaseAdapter> adapter(
        EncosDriverManager::Instance().CreateAdapterWithFactory(interface_name, [interface_name]() {
            return CreateRelayWsAdapterStatic(interface_name, "RelayWsEmptyFrameTest",
                                              LogLevel::Info);
        }));
    ASSERT_TRUE(adapter->Ok());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    while (server.EmptyFrameCount() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_GT(server.EmptyFrameCount(), 0);
}

TEST(RelayWsAdapterTests, CascadeDeletionDrainsReceiveCallbackBeforeJoiningWorker) {
    const int port = ix::getFreePort();
    RelayParameterServer server(port);
    ASSERT_TRUE(server.Start()) << server.LastError();

    const auto interface_name = "ws://127.0.0.1:" + std::to_string(port) + "/ws";
    auto& manager = EncosDriverManager::Instance();
    auto* adapter = manager.CreateAdapterWithFactory(interface_name, [interface_name]() {
        return CreateRelayWsAdapterStatic(interface_name, "RelayWsCascadeDeleteTest",
                                          LogLevel::Info);
    });
    ASSERT_NE(adapter, nullptr);
    auto* motor = adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);

    std::mutex callback_mutex;
    std::condition_variable callback_condition;
    bool callback_entered = false;
    bool release_callback = false;
    std::atomic<int> callback_count{0};
    motor->SetOnStatus([&](const MotorStatus&) {
        std::unique_lock<std::mutex> lock(callback_mutex);
        ++callback_count;
        callback_entered = true;
        callback_condition.notify_all();
        callback_condition.wait(lock, [&]() {
            return release_callback;
        });
    });

    MotorMessage feedback{};
    feedback.bus_idx = 0;
    feedback.data.id = 1;
    feedback.data.len = 8;
    feedback.data.data[0] = 0x20;
    ASSERT_TRUE(server.SendRecords({feedback}));
    {
        std::unique_lock<std::mutex> lock(callback_mutex);
        ASSERT_TRUE(callback_condition.wait_for(lock, std::chrono::seconds(3), [&]() {
            return callback_entered;
        }));
    }

    WaitObserver wait_observer;
    DriverManagerTestAccess::SetWaitHook(manager, [&] {
        wait_observer.Notify();
    });
    auto deletion = std::async(std::launch::async, [&]() {
        return manager.DestroyAdapter(adapter);
    });
    wait_observer.WaitForCount(1U);
    DriverManagerTestAccess::SetWaitHook(manager, {});
    {
        std::lock_guard<std::mutex> lock(callback_mutex);
        release_callback = true;
    }
    callback_condition.notify_all();
    ASSERT_EQ(deletion.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_TRUE(deletion.get());

    (void) server.SendRecords({feedback});
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(callback_count.load(), 1);
}

TEST(RelayWsClientQueueTests, OversizedFrameDoesNotEvictRetainedFrames) {
    RelayWsClient client;
    std::vector<std::vector<uint8_t>> delivered;
    client.SetOnMessage([&delivered](const std::vector<uint8_t>& message) {
        delivered.push_back(message);
    });

    const std::vector<uint8_t> retained{0x42};
    const std::vector<uint8_t> oversized((8u * 1024u * 1024u) + 1u, 0xA5);
    client.OnMessage(retained.data(), static_cast<int>(retained.size()));
    client.OnMessage(oversized.data(), static_cast<int>(oversized.size()));
    client.Poll();

    ASSERT_EQ(delivered.size(), 1);
    EXPECT_EQ(delivered.front(), retained);
    EXPECT_EQ(client.TakeDroppedIncomingCount(), 1);
}

TEST(RelayWsQueueTests, FailedBatchReinsertionDropsOldestAcrossCombinedQueue) {
    auto message = [](std::uint32_t id) {
        MotorMessage result{};
        result.data.id = id;
        return result;
    };
    std::vector<MotorMessage> current{message(4), message(5), message(6)};
    std::vector<MotorMessage> failed{message(1), message(2), message(3)};

    ReinsertFailedRelayMessages(current, std::move(failed), 4);

    ASSERT_EQ(current.size(), 4);
    EXPECT_EQ(current[0].data.id, 3u);
    EXPECT_EQ(current[1].data.id, 4u);
    EXPECT_EQ(current[2].data.id, 5u);
    EXPECT_EQ(current[3].data.id, 6u);
}

TEST(RelayWsQueueTests, SameBusGenerationsUseSeparateEmr1Frames) {
    RelayQueuedMessage first{};
    first.message.bus_idx = 0;
    first.message.data.id = 1;
    first.generation = 1;
    RelayQueuedMessage other_bus{};
    other_bus.message.bus_idx = 1;
    other_bus.message.data.id = 2;
    other_bus.generation = 0;
    RelayQueuedMessage second{};
    second.message.bus_idx = 0;
    second.message.data.id = 3;
    second.generation = 2;

    const auto encoded =
        EncodeQueuedRelayFrames(RelayFrameType::RelayToHelper, {first, other_bus, second});
    const auto decoded = DecodeRelayFrames(encoded);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 2U);
    ASSERT_EQ((*decoded)[0].records.size(), 2U);
    EXPECT_EQ((*decoded)[0].records[0].data.id, 1U);
    EXPECT_EQ((*decoded)[0].records[1].data.id, 2U);
    ASSERT_EQ((*decoded)[1].records.size(), 1U);
    EXPECT_EQ((*decoded)[1].records[0].data.id, 3U);
}

TEST(RelayWsQueueTests, GenerationAwareFramesSplitAtRecordLimit) {
    std::vector<RelayQueuedMessage> messages;
    messages.reserve(256U);
    for (std::uint32_t id = 0; id < 256U; ++id) {
        RelayQueuedMessage message{};
        message.message.bus_idx = 0;
        message.message.data.id = id;
        message.generation = 1;
        messages.push_back(message);
    }

    const auto decoded =
        DecodeRelayFrames(EncodeQueuedRelayFrames(RelayFrameType::RelayToHelper, messages));

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 2U);
    ASSERT_EQ((*decoded)[0].records.size(), 255U);
    ASSERT_EQ((*decoded)[1].records.size(), 1U);
    EXPECT_EQ((*decoded)[0].records.front().data.id, 0U);
    EXPECT_EQ((*decoded)[0].records.back().data.id, 254U);
    EXPECT_EQ((*decoded)[1].records.front().data.id, 255U);
}

TEST(RelayWsQueueTests, TaggedFailedBatchReinsertionPreservesGenerations) {
    auto message = [](std::uint32_t id, std::size_t generation) {
        RelayQueuedMessage result{};
        result.message.data.id = id;
        result.generation = generation;
        return result;
    };
    std::vector<RelayQueuedMessage> current{message(4, 2), message(5, 2), message(6, 3)};
    std::vector<RelayQueuedMessage> failed{message(1, 1), message(2, 1), message(3, 1)};

    ReinsertFailedRelayMessages(current, std::move(failed), 4U);

    ASSERT_EQ(current.size(), 4U);
    EXPECT_EQ(current[0].message.data.id, 3U);
    EXPECT_EQ(current[0].generation, 1U);
    EXPECT_EQ(current[1].message.data.id, 4U);
    EXPECT_EQ(current[1].generation, 2U);
    EXPECT_EQ(current[3].message.data.id, 6U);
    EXPECT_EQ(current[3].generation, 3U);
}

TEST(RelayWsWireTests, AutoFallbackCapabilityCorrelationAndReset) {
    RelayWsWireState wire;
    EXPECT_EQ(wire.options.protocol, RelayProtocol::Emr1);
    RelayCapabilityMessage response;
    response.response = true;
    response.request_id = wire.request_id;
    response.capabilities = {true, 128, 0};
    wire.Receive(response);
    wire.ApplyPending();
    EXPECT_EQ(wire.options.protocol, RelayProtocol::Emr1);
    wire.requested = true;
    wire.Receive(response);
    EXPECT_EQ(wire.options.protocol, RelayProtocol::Emr1);
    wire.ApplyPending();
    EXPECT_EQ(wire.options.protocol, RelayProtocol::Emr2);
    EXPECT_EQ(wire.options.max_payload, 128U);
    wire.Reset();
    wire.requested = true;
    wire.Receive(response);
    wire.ApplyPending();
    EXPECT_EQ(wire.options.protocol, RelayProtocol::Emr1);
    EXPECT_EQ(wire.options.max_payload, 1448U);
}

TEST(RelayWsWireTests, ExplicitModesAndInvalidConfiguration) {
    RelayWsWireState forced(R"({"protocol":"emr2","max_payload":128})");
    EXPECT_EQ(forced.options.protocol, RelayProtocol::Emr2);
    forced.Reset();
    EXPECT_EQ(forced.options.protocol, RelayProtocol::Emr2);
    EXPECT_THROW(RelayWsWireState(R"({"protocol":"unknown"})"), std::invalid_argument);
    EXPECT_THROW(RelayWsWireState(R"({"max_payload":99})"), std::invalid_argument);
    EXPECT_THROW(RelayWsWireState(R"({"buses":{"0":{"protocol":"emr2"}}})"), std::invalid_argument);
}

TEST(RelayWsQueueTests, Emr2PacketsRespectBudgetAndGeneration) {
    RelayQueuedMessage first{};
    first.message.data.len = 64;
    first.message.data.frame_flags = kCanFrameFlagFdMask;
    first.generation = 1;
    auto second = first;
    second.generation = 2;
    RelayWireOptions options;
    options.protocol = RelayProtocol::Emr2;
    options.max_payload = 128;
    options.batch_id = 17;
    const auto packets =
        EncodeQueuedRelayPackets(RelayFrameType::RelayToHelper, {first, first, second}, options);
    ASSERT_EQ(packets.size(), 3U);
    EXPECT_EQ(options.batch_id, 18U);
    for (const auto& packet : packets) {
        EXPECT_LE(packet.size(), 128U);
        const auto decoded = DecodeRelayPackets(packet);
        ASSERT_TRUE(decoded);
        ASSERT_EQ(decoded->size(), 1U);
        ASSERT_EQ(decoded->front().records.size(), 1U);
        EXPECT_EQ(decoded->front().records.front().message.data.len, 64U);
        EXPECT_EQ(decoded->front().batch_id, 17U);
    }
}

TEST(RelayWsQueueTests, InvalidBatchDoesNotAdvanceSequence) {
    RelayQueuedMessage valid{};
    auto invalid = valid;
    invalid.message.data.len = 9;
    RelayWireOptions options;
    options.sequence = 42;
    EXPECT_THROW(EncodeQueuedRelayPackets(RelayFrameType::RelayToHelper, {valid, invalid}, options),
                 std::invalid_argument);
    EXPECT_EQ(options.sequence, 42U);
    EXPECT_EQ(options.batch_id, 0U);
}

TEST(RelayWsAdapterTests, NegotiatesEmr2AndSendsIndependentBoundedPackets) {
    const int port = ix::getFreePort();
    RelayParameterServer server(port, true);
    ASSERT_TRUE(server.Start()) << server.LastError();
    const auto url = "ws://127.0.0.1:" + std::to_string(port) + "/ws";
    ManagedAdapterGuard<BaseAdapter> managed(
        EncosDriverManager::Instance().CreateAdapterWithFactory(url, [&]() {
            return new TestRelayWsAdapter(url, "RelayWsEmr2Test", LogLevel::Info);
        }));
    auto* adapter = static_cast<TestRelayWsAdapter*>(managed.get());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (server.Emr2Packets() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_GT(server.Emr2Packets(), 0);
    MotorMessage message{};
    message.data.len = 64;
    message.data.frame_flags = kCanFrameFlagFdMask;
    adapter->SendSynchronized({message, message, message});
    while (server.LargeRecords() < 3 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto* motor = adapter->GetBus(0)->GetMotor(1, MotorModel::EC_A4310_P2);
    EXPECT_NEAR(motor->GetParameter<MotorParameter::Position>(), kExpectedPositionRad, 0.001f);
    EXPECT_EQ(server.LargeRecords(), 3);
    EXPECT_EQ(server.OversizedPackets(), 0);
}

TEST(RelayWsAdapterTests, LegacyBatchRejectsOversizedRecordBeforeEnqueue) {
    const int port = ix::getFreePort();
    RelayParameterServer server(port);
    ASSERT_TRUE(server.Start()) << server.LastError();
    const auto url = "ws://127.0.0.1:" + std::to_string(port) + "/ws";
    ManagedAdapterGuard<BaseAdapter> managed(
        EncosDriverManager::Instance().CreateAdapterWithFactory(url, [&]() {
            return new TestRelayWsAdapter(url, "RelayWsInvalidBatchTest", LogLevel::Info);
        }));
    auto* adapter = static_cast<TestRelayWsAdapter*>(managed.get());
    MotorMessage valid{};
    valid.data.len = 2;
    valid.data.data[0] = static_cast<uint8_t>(0x07 << 5);
    valid.data.data[1] = static_cast<uint8_t>(MotorParameter::Position);
    auto invalid = valid;
    invalid.data.len = 64;
    EXPECT_THROW(adapter->SendSynchronized({valid, invalid}), std::invalid_argument);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(server.RequestCount(), 0);
    EXPECT_EQ(server.Emr2Packets(), 0);
}

}  // namespace encos
