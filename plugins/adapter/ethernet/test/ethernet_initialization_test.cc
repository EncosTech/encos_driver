// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

#include "adapter/adapter_extend.h"
#include "ethernet/ethernet_management.h"
#include "ethernet/ethernet_timing.h"
#include "ethernet/protocol/gateway_wire.h"
#include "ethernet/transport/datagram.h"
#include "external/json.hpp"

namespace encos::ethernet {
namespace {
class MemoryChannel : public DatagramChannel {
public:
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<std::vector<uint8_t>> incoming;
    MemoryChannel* peer = nullptr;
    bool Send(const uint8_t* data, std::size_t size) override {
        {
            std::lock_guard<std::mutex> lock(peer->mutex);
            peer->incoming.emplace_back(data, data + size);
        }
        peer->condition.notify_one();
        return true;
    }
    std::size_t Receive(uint8_t* data, std::size_t capacity) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (incoming.empty())
            return 0;
        auto packet = std::move(incoming.front());
        incoming.pop_front();
        if (packet.size() > capacity)
            return 0;
        std::copy(packet.begin(), packet.end(), data);
        return packet.size();
    }
    bool Wait(int milliseconds) override {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, std::chrono::milliseconds(milliseconds), [&] {
            return !incoming.empty();
        });
    }
};
class Gateway {
public:
    DatagramChannel& client() {
        return channels_[0];
    }
    std::atomic<unsigned> sets{0}, mask{0}, unsubscribes{0};
    // 逐通道的期望与实测网关 CAN 时序，使"逐通道覆盖"与"全局时序"用例复用同一替身
    std::array<std::atomic<std::uint32_t>, kGatewayChannelCount> expected_nominal{};
    std::array<std::atomic<std::uint32_t>, kGatewayChannelCount> expected_data{};
    std::array<std::atomic<std::uint32_t>, kGatewayChannelCount> expected_nominal_sp{};
    std::array<std::atomic<std::uint32_t>, kGatewayChannelCount> expected_data_sp{};
    std::array<std::atomic<std::uint32_t>, kGatewayChannelCount> last_nominal{};
    std::array<std::atomic<std::uint32_t>, kGatewayChannelCount> last_data{};
    std::array<std::atomic<std::uint32_t>, kGatewayChannelCount> last_nominal_sp{};
    std::array<std::atomic<std::uint32_t>, kGatewayChannelCount> last_data_sp{};
    /** @brief 把 8 路通道的期望时序设为同一组值（atomic 不可拷贝，故逐个 store） */
    void ExpectUniform(std::uint32_t nominal, std::uint32_t data, std::uint32_t nominal_sp,
                       std::uint32_t data_sp) {
        for (unsigned channel = 0; channel < kGatewayChannelCount; ++channel) {
            expected_nominal[channel].store(nominal);
            expected_data[channel].store(data);
            expected_nominal_sp[channel].store(nominal_sp);
            expected_data_sp[channel].store(data_sp);
        }
    }
    explicit Gateway(unsigned failure = 0, unsigned channel_count = 8) {
        ExpectUniform(1000000, 5000000, 750, 875);
        channels_[0].peer = &channels_[1];
        channels_[1].peer = &channels_[0];
        worker_ = std::thread([this, failure, channel_count] {
            while (running_) {
                if (!channels_[1].Wait(20))
                    continue;
                std::array<uint8_t, 64> packet{};
                if (channels_[1].Receive(packet.data(), packet.size()) != 64)
                    continue;
                const auto type = packet[5];
                if (type == GW_UNSUBSCRIBE) {
                    ++unsubscribes;
                    continue;
                }
                if (type == GW_SUBSCRIBE) {
                    packet[5] = GW_SUBSCRIBED;
                    packet[12] = 42;
                    packet[46] = failure == 3 ? 3 : 7;
                    packet[47] = static_cast<uint8_t>(channel_count);
                } else if (type == GW_CAN_SET) {
                    const auto channel = gw_get32(packet.data() + 28);
                    EXPECT_LT(channel, channel_count);
                    if (channel >= kGatewayChannelCount)
                        continue;
                    EXPECT_EQ(packet[12], 42);
                    last_nominal[channel].store(gw_get32(packet.data() + 32));
                    last_data[channel].store(gw_get32(packet.data() + 36));
                    last_nominal_sp[channel].store(gw_get32(packet.data() + 40));
                    last_data_sp[channel].store(gw_get32(packet.data() + 44));
                    EXPECT_EQ(last_nominal[channel].load(), expected_nominal[channel].load());
                    EXPECT_EQ(last_data[channel].load(), expected_data[channel].load());
                    EXPECT_EQ(last_nominal_sp[channel].load(), expected_nominal_sp[channel].load());
                    EXPECT_EQ(last_data_sp[channel].load(), expected_data_sp[channel].load());
                    ++sets;
                    if (failure == 4)
                        continue;
                    // Lose the first SET response to exercise idempotent retry.
                    if (sets == 1 && failure == 0)
                        continue;
                    mask.fetch_or(1U << channel);
                    auto event = packet;
                    event[5] = GW_CAN_EVENT;
                    gw_put32(event.data() + 32, channel);
                    gw_put32(event.data() + 36, 1U << 25);
                    (void) channels_[1].Send(event.data(), event.size());
                    packet[5] = GW_CAN_STATE;
                    gw_put32(packet.data() + 52, 1);
                    if (failure == 1)
                        gw_put32(packet.data() + 48, 2);
                    if (failure == 2)
                        gw_put32(packet.data() + 36, 1000000);
                    // An unrelated request ID must not advance initialization.
                    auto unrelated = packet;
                    gw_put32(unrelated.data() + 8, gw_get32(packet.data() + 8) + 1);
                    (void) channels_[1].Send(unrelated.data(), unrelated.size());
                } else
                    continue;
                (void) channels_[1].Send(packet.data(), packet.size());
            }
        });
    }
    ~Gateway() {
        running_ = false;
        worker_.join();
    }

private:
    MemoryChannel channels_[2];
    std::atomic<bool> running_{true};
    std::thread worker_;
};
}  // namespace
TEST(EthernetInitialization, ConfiguresEveryChannelOnEveryInitializationAndRetries) {
    Gateway gateway;
    unsigned events = 0;
    EXPECT_EQ(InitializeCanChannels(gateway.client(), GatewayChannelTimings{},
                                    [&](const CanError& event) {
                                        EXPECT_LT(event.channel, 8U);
                                        EXPECT_EQ(event.flags, 1U << 25);
                                        ++events;
                                    })
                  .identity[0],
              42);
    EXPECT_EQ(events, 8U);
    EXPECT_EQ(gateway.mask.load(), 255U);
    EXPECT_EQ(gateway.sets.load(), 9U);
    EXPECT_EQ(InitializeCanChannels(gateway.client()).identity[0], 42);
    EXPECT_EQ(gateway.sets.load(), 17U);
}
TEST(EthernetInitialization, SupportsTwoAndThreeChannelGateways) {
    for (const unsigned count : {2U, 3U}) {
        Gateway gateway(0, count);
        const auto initialized = InitializeCanChannels(gateway.client());
        EXPECT_EQ(initialized.channel_count, count);
        EXPECT_EQ(initialized.identity[0], 42);
        EXPECT_EQ(gateway.mask.load(), (1U << count) - 1);
        EXPECT_EQ(gateway.sets.load(), count + 1);
    }
}
TEST(EthernetInitialization, RejectsFailureMismatchAndUnsupportedFirmware) {
    for (unsigned failure : {1U, 2U, 3U}) {
        Gateway gateway(failure);
        EXPECT_THROW(InitializeCanChannels(gateway.client()), std::runtime_error);
        EXPECT_EQ(gateway.sets.load(), failure == 3 ? 0U : 1U);
    }
}
TEST(EthernetInitialization, TimesOutWithoutProceedingToOtherChannels) {
    Gateway gateway(4);
    EXPECT_THROW(InitializeCanChannels(gateway.client()), std::runtime_error);
    EXPECT_GT(gateway.sets.load(), 1U);
    EXPECT_EQ(gateway.mask.load(), 0U);
}
namespace {

/** @brief 便捷：把信封文本应用到某从站某通道的生效时序 */
GatewayCanTiming ChannelTimingFromEnvelope(const std::string& text, int slave, unsigned channel) {
    const auto extend = AdapterExtend::Parse(text);
    return BuildGatewayChannelTimings(extend, slave)[channel];
}

/** @brief 8 路通道统一使用同一份时序 */
GatewayChannelTimings UniformTimings(const GatewayCanTiming& timing) {
    GatewayChannelTimings timings;
    timings.fill(timing);
    return timings;
}

}  // namespace

TEST(EthernetInitialization, UsesConfiguredTimingWithPerMilleSamplePoints) {
    Gateway gateway;
    gateway.ExpectUniform(500000, 2000000, 875, 750);

    GatewayCanTiming timing;
    timing.nominal_bps = 500000;
    timing.data_bps = 2000000;
    timing.nominal_sample_point = 0.875;
    timing.data_sample_point = 0.75;

    EXPECT_EQ(InitializeCanChannels(gateway.client(), UniformTimings(timing)).identity[0], 42);
    EXPECT_EQ(gateway.last_nominal[0].load(), 500000U);
    EXPECT_EQ(gateway.last_data[0].load(), 2000000U);
    EXPECT_EQ(gateway.last_nominal_sp[0].load(), 875U);
    EXPECT_EQ(gateway.last_data_sp[0].load(), 750U);
}

TEST(EthernetInitialization, ConfiguresEachChannelWithItsOwnTiming) {
    Gateway gateway;
    // 仅 slave 2 的通道 5 被覆盖为 500 kbit/s，其余通道保持 1 Mbit/s
    gateway.expected_nominal[5].store(500000);

    const auto timings = BuildGatewayChannelTimings(
        AdapterExtend::Parse(R"({"bitrate": 1000000, "buses": {"2:5": {"bitrate": 500000}}})"), 2);
    EXPECT_EQ(InitializeCanChannels(gateway.client(), timings).identity[0], 42);
    EXPECT_EQ(gateway.last_nominal[5].load(), 500000U);
    EXPECT_EQ(gateway.last_nominal[4].load(), 1000000U);
    EXPECT_EQ(gateway.last_nominal[0].load(), 1000000U);
}

TEST(EthernetInitialization, EmptyEnvelopeKeepsGatewayDefaults) {
    const auto timing = ChannelTimingFromEnvelope("", 0, 0);
    EXPECT_EQ(timing.nominal_bps, 1000000U);
    EXPECT_EQ(timing.data_bps, 5000000U);
    EXPECT_DOUBLE_EQ(timing.nominal_sample_point, 0.75);
    EXPECT_DOUBLE_EQ(timing.data_sample_point, 0.875);
    EXPECT_EQ(ToPerMille(timing.nominal_sample_point), 750U);
    EXPECT_EQ(ToPerMille(timing.data_sample_point), 875U);
}

TEST(EthernetInitialization, PartialEnvelopeKeepsRemainingDefaults) {
    const GatewayCanTiming defaults;
    const auto timing = ChannelTimingFromEnvelope(R"({"bitrate": 500000})", 0, 0);
    EXPECT_EQ(timing.nominal_bps, 500000U);
    EXPECT_EQ(timing.data_bps, defaults.data_bps);
    EXPECT_DOUBLE_EQ(timing.nominal_sample_point, defaults.nominal_sample_point);
    EXPECT_DOUBLE_EQ(timing.data_sample_point, defaults.data_sample_point);
}

TEST(EthernetInitialization, RejectsInvalidTimingFields) {
    const auto rejects = [](const std::string& text) {
        EXPECT_THROW(ChannelTimingFromEnvelope(text, 0, 0), std::runtime_error) << text;
    };
    rejects(R"({"bitrate": 0})");
    rejects(R"({"dbitrate": -5})");
    rejects(R"({"bitrate": 1.5})");
    rejects(R"({"sample_point": 1.5})");
    rejects(R"({"sample_point": 0.0})");
    rejects(R"({"dsample_point": "high"})");
    // 未知字段
    rejects(R"({"bitrat": 500000})");
}

TEST(EthernetInitialization, SamplePointUsesTheSameScaleAsCan) {
    const auto timing = ChannelTimingFromEnvelope(R"({"sample_point": 0.765})", 0, 0);
    EXPECT_DOUBLE_EQ(timing.nominal_sample_point, 0.765);
    EXPECT_EQ(ToPerMille(timing.nominal_sample_point), 765U);
}
TEST(EthernetInitialization, RejectsInvalidChannelCountsBeforeAnyCanSet) {
    for (const unsigned count : {0U, 9U, 255U}) {
        Gateway gateway(0, count);
        EXPECT_THROW(InitializeCanChannels(gateway.client()), std::runtime_error);
        EXPECT_EQ(gateway.sets.load(), 0U);
    }
}

TEST(EthernetInitialization, ValidatesOverridesAfterHandshakeBeforeSendingAnyCanSet) {
    for (const unsigned count : {2U, 3U, 8U}) {
        Gateway gateway(0, count);
        const auto extend = AdapterExtend::Parse("{\"buses\":{\"5:" + std::to_string(count) +
                                                 "\":{\"bitrate\":500000}}}");
        EXPECT_THROW(InitializeCanChannels(gateway.client(),
                                           [&](unsigned actual) {
                                               EXPECT_EQ(actual, count);
                                               return BuildGatewayChannelTimings(extend, 5, actual);
                                           }),
                     std::runtime_error);
        EXPECT_EQ(gateway.sets.load(), 0U);
    }
}

TEST(EthernetInitialization, AppliesOverridesOnLastActualChannel) {
    for (const unsigned count : {2U, 3U, 8U}) {
        Gateway gateway(0, count);
        gateway.expected_nominal[count - 1].store(500000);
        const auto extend = AdapterExtend::Parse("{\"buses\":{\"5:" + std::to_string(count - 1) +
                                                 "\":{\"bitrate\":500000}}}");
        const auto initialized = InitializeCanChannels(gateway.client(), [&](unsigned actual) {
            return BuildGatewayChannelTimings(extend, 5, actual);
        });
        EXPECT_EQ(initialized.channel_count, count);
        EXPECT_EQ(initialized.timings[count - 1].nominal_bps, 500000U);
        EXPECT_EQ(gateway.mask.load(), (1U << count) - 1);
    }
}

}  // namespace encos::ethernet
