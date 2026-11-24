// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

#include "adapter/base_adapter.h"
#include "node/native_statistics.h"

namespace encos {

namespace {

class StatisticsAdapter final : public BaseAdapter {
public:
    StatisticsAdapter() : BaseAdapter("statistics-test", "StatisticsTest", LogLevel::Off) {}

    using BaseAdapter::InitializeStatistics;
    using BaseAdapter::UpdateStatistics;

    std::unordered_map<int, Bus*> GetBuses() override {
        return {};
    }

    bool Ok() override {
        return true;
    }

    void Send(const MotorMessage&) override {}
};

TEST(AdapterStatistics, UnsupportedPendingZeroAndValueIsolation) {
    StatisticsAdapter adapter;
    EXPECT_FALSE(adapter.GetStatistics());

    BusStatistics bus;
    bus.bus_id = 7;
    adapter.InitializeStatistics(AdapterStatistics{{bus}}, 500);
    const auto pending = adapter.GetStatistics();

    ASSERT_TRUE(pending);
    ASSERT_EQ(pending->buses.size(), 1U);
    EXPECT_EQ(pending->buses[0].status, StatisticsStatus::Pending);
    EXPECT_EQ(pending->buses[0].slave_id, 0);
    EXPECT_FALSE(pending->buses[0].age_ms);
    EXPECT_FALSE(pending->buses[0].error_counters);

    bus.error_counters = CanErrorCounters{};
    adapter.UpdateStatistics(bus);
    auto valid = adapter.GetStatistics();

    EXPECT_EQ(valid->buses[0].status, StatisticsStatus::Valid);
    ASSERT_TRUE(valid->buses[0].error_counters);
    EXPECT_EQ(valid->buses[0].error_counters->tx, 0U);
    EXPECT_EQ(valid->buses[0].error_counters->rx, 0U);
    EXPECT_EQ(valid->buses[0].error_counters->kind, ErrorCounterKind::Current);

    valid->buses[0].error_counters->tx = 99;
    EXPECT_EQ(adapter.GetStatistics()->buses[0].error_counters->tx, 0U);

    bus.slave_id = 1;
    bus.error_counters->tx = 12;
    adapter.UpdateStatistics(bus);
    EXPECT_EQ(adapter.GetStatistics()->buses.size(), 1U);
    EXPECT_EQ(adapter.GetStatistics()->buses[0].error_counters->tx, 0U);
}

TEST(AdapterStatistics, SamplesBecomeStaleAndRefreshWithoutLosingZero) {
    StatisticsAdapter adapter;
    BusStatistics bus;
    bus.error_counters = CanErrorCounters{};
    adapter.InitializeStatistics(AdapterStatistics{{bus}}, 1);
    adapter.UpdateStatistics(bus);

    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    EXPECT_EQ(adapter.GetStatistics()->buses[0].status, StatisticsStatus::Stale);

    adapter.UpdateStatistics(bus);
    EXPECT_EQ(adapter.GetStatistics()->buses[0].error_counters->tx, 0U);
}

TEST(AdapterStatistics, ConcurrentSnapshotsNeverObserveTornFields) {
    StatisticsAdapter adapter;
    BusStatistics bus;
    adapter.InitializeStatistics(AdapterStatistics{{bus}}, 0);

    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (unsigned n = 0; n < 1000; ++n) {
            bus.error_counters = CanErrorCounters{n, n, ErrorCounterKind::Current};
            adapter.UpdateStatistics(bus);
        }
        done = true;
    });

    do {
        const auto snapshot = adapter.GetStatistics();
        if (snapshot->buses[0].error_counters)
            EXPECT_EQ(snapshot->buses[0].error_counters->tx, snapshot->buses[0].error_counters->rx);
    } while (!done.load());

    writer.join();
}

TEST(AdapterStatistics, NativeJsonDistinguishesUnsupportedPendingZeroEnumsAndAddresses) {
    EXPECT_TRUE(node::StatisticsJson(std::nullopt).is_null());

    BusStatistics pending;

    BusStatistics valid;
    valid.slave_id = 2;
    valid.bus_id = 7;
    valid.status = StatisticsStatus::Valid;
    valid.age_ms = 0;
    valid.error_counters = CanErrorCounters{};

    BusStatistics stale;
    stale.status = StatisticsStatus::Stale;
    stale.error_counters = CanErrorCounters{12, 37, ErrorCounterKind::LastErrorEvent};

    const auto result = node::StatisticsJson(AdapterStatistics{{pending, valid, stale}});
    const auto expected = nlohmann::json::parse(R"({"buses":[
      {"slaveId":0,"busId":0,"status":"pending"},
      {"slaveId":2,"busId":7,"status":"valid","ageMs":0,"errorCounters":{"tx":0,"rx":0,"kind":"current"}},
      {"slaveId":0,"busId":0,"status":"stale","errorCounters":{"tx":12,"rx":37,"kind":"last-error-event"}}
    ]})");

    EXPECT_EQ(result, expected);
}

}  // namespace

}  // namespace encos
