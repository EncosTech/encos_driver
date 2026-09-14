#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

#include "ethernet/ethernet_management.h"
#include "ethernet/protocol/gateway_wire.h"
#include "ethernet/transport/datagram.h"

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
    explicit Gateway(unsigned failure = 0) {
        channels_[0].peer = &channels_[1];
        channels_[1].peer = &channels_[0];
        worker_ = std::thread([this, failure] {
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
                    packet[47] = 8;
                } else if (type == GW_CAN_SET) {
                    const auto channel = gw_get32(packet.data() + 28);
                    EXPECT_LT(channel, 8U);
                    EXPECT_EQ(packet[12], 42);
                    EXPECT_EQ(gw_get32(packet.data() + 32), 1000000U);
                    EXPECT_EQ(gw_get32(packet.data() + 36), 5000000U);
                    EXPECT_EQ(gw_get32(packet.data() + 40), 750U);
                    EXPECT_EQ(gw_get32(packet.data() + 44), 875U);
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
    EXPECT_EQ(InitializeCanChannels(gateway.client(),
                                    [&](const CanError& event) {
                                        EXPECT_LT(event.channel, 8U);
                                        EXPECT_EQ(event.flags, 1U << 25);
                                        ++events;
                                    })[0],
              42);
    EXPECT_EQ(events, 8U);
    EXPECT_EQ(gateway.mask.load(), 255U);
    EXPECT_EQ(gateway.sets.load(), 9U);
    EXPECT_EQ(InitializeCanChannels(gateway.client())[0], 42);
    EXPECT_EQ(gateway.sets.load(), 17U);
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
}  // namespace encos::ethernet
