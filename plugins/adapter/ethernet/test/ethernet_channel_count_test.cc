// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "ethernet/ethernet_adapter.h"
#include "managed_adapter_test.h"

namespace encos {
namespace {
class IdleTransport : public ethernet::Transport {
public:
    bool Send(unsigned, const MotorMessages&) override {
        return true;
    }
    MotorMessages Receive() override {
        return {};
    }
    void Wait(int) override {}
    void Notify() override {}
};
}  // namespace
class EthernetAdapterTestAccess {
public:
    static ManagedAdapterGuard<EthernetAdapter> Create(bool multi) {
        auto* adapter = static_cast<EthernetAdapter*>(
            EncosDriverManager::Instance().CreateAdapterWithFactory("ethernet-test", [multi] {
                auto adapter = std::unique_ptr<EthernetAdapter>(new EthernetAdapter());
                adapter->multi_ = multi;
                adapter->peers_[multi ? 1 : 0].channel_count = 2;
                if (multi) {
                    adapter->peers_[2].channel_count = 3;
                    adapter->peers_[3].channel_count = 8;
                }
                adapter->transport_ = std::make_unique<IdleTransport>();
                adapter->running_ = true;
                return adapter.release();
            }));
        return ManagedAdapterGuard<EthernetAdapter>(adapter);
    }
    static void Send(EthernetAdapter& adapter, const MotorMessages& messages) {
        adapter.SendSynchronized(messages);
    }
    static std::size_t Queued(EthernetAdapter& adapter, unsigned slave) {
        return adapter.peers_.at(slave).pending.Pop().size();
    }
};

TEST(EthernetChannelCount, EnumeratesActualBusesOnMixedSlaves) {
    auto adapter = EthernetAdapterTestAccess::Create(true);
    const auto buses = adapter->GetBuses();
    EXPECT_EQ(buses.size(), 13U);
    for (const auto [slave, count] : std::map<int, int>{{1, 2}, {2, 3}, {3, 8}}) {
        for (int channel = 0; channel < count; ++channel) {
            EXPECT_EQ(buses.count((slave << 16) | channel), 1U);
        }
        EXPECT_EQ(buses.count((slave << 16) | count), 0U);
    }
}

TEST(EthernetChannelCount, RejectsOutOfRangeBatchWithoutQueueingValidPrefix) {
    auto adapter = EthernetAdapterTestAccess::Create(true);
    MotorMessage valid{}, invalid{};
    valid.bus_idx = (1 << 16) | 1;
    valid.data.id = 1;
    valid.data.len = 1;
    invalid = valid;
    invalid.bus_idx = (2 << 16) | 3;
    EXPECT_THROW(EthernetAdapterTestAccess::Send(*adapter, {valid, invalid}),
                 std::invalid_argument);
    EXPECT_EQ(EthernetAdapterTestAccess::Queued(*adapter, 1), 0U);
    EXPECT_NO_THROW(EthernetAdapterTestAccess::Send(*adapter, {valid}));
    EXPECT_EQ(EthernetAdapterTestAccess::Queued(*adapter, 1), 1U);
}

TEST(EthernetChannelCount, DirectConnectionUsesOnlyLocalActualChannels) {
    auto adapter = EthernetAdapterTestAccess::Create(false);
    const auto buses = adapter->GetBuses();
    EXPECT_EQ(buses.size(), 2U);
    EXPECT_EQ(buses.count(1), 1U);
    MotorMessage message{};
    message.data.id = 1;
    message.data.len = 1;
    for (const int invalid : {-1, 2, (1 << 16) | 1}) {
        message.bus_idx = invalid;
        EXPECT_THROW(EthernetAdapterTestAccess::Send(*adapter, {message}), std::invalid_argument);
    }
    message.bus_idx = 1;
    EXPECT_NO_THROW(EthernetAdapterTestAccess::Send(*adapter, {message}));
}

TEST(EthernetChannelCount, UpdateChecksAllChannelRangesBeforeApplyingAnyBus) {
    auto adapter = EthernetAdapterTestAccess::Create(true);
    try {
        adapter->UpdateBusExtend(
            R"({"buses":{"1:0":{"bitrate":500000},"2:3":{"bitrate":500000}}})");
        FAIL() << "Out-of-range channel accepted";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("2:3"), std::string::npos);
    }
}
}  // namespace encos
