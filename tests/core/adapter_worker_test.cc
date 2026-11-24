// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <atomic>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../../plugins/adapter/utils/statistics_wait.h"
#include "../../plugins/adapter/utils/worker_startup.h"
#include "utils/thread_priority.h"

namespace encos {

namespace {

using namespace std::chrono_literals;

TEST(AdapterWorkerStartup, InitializesOnceOnTheRealWorkerBeforeRunning) {
    const auto caller_id = std::this_thread::get_id();
    std::thread::id initialized_id;
    std::promise<std::thread::id> running_id;
    auto ran = running_id.get_future();
    std::promise<void> finish;
    auto allowed_to_finish = finish.get_future();
    int initializes = 0;
    std::thread worker;

    adapter_worker::Start(
        worker,
        [&] {
            initialized_id = std::this_thread::get_id();
            ++initializes;
        },
        [&] {
            running_id.set_value(std::this_thread::get_id());
            allowed_to_finish.wait();
        });

    const auto actual_id = ran.get();
    EXPECT_EQ(initializes, 1);
    EXPECT_NE(initialized_id, caller_id);
    EXPECT_EQ(initialized_id, actual_id);
    EXPECT_EQ(actual_id, worker.get_id());

    finish.set_value();
    worker.join();
}

TEST(AdapterWorkerStartup, WaitsForAuthorizationInsteadOfReportingSuccessEarly) {
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> authorize;
    auto authorization = authorize.get_future();
    std::thread worker;
    auto constructor = std::async(std::launch::async, [&] {
        adapter_worker::Start(
            worker,
            [&] {
                entered.set_value();
                authorization.wait();
            },
            [] {});
    });

    entered_future.wait();
    EXPECT_EQ(constructor.wait_for(20ms), std::future_status::timeout);

    authorize.set_value();
    constructor.get();
    worker.join();
}

template <typename Error>
void ExpectOriginalStartupFailure(const char* message) {
    std::thread worker;
    bool ran = false;
    try {
        adapter_worker::Start(
            worker,
            [message] {
                throw Error(message);
            },
            [&] {
                ran = true;
            });
        ADD_FAILURE() << "Initialization unexpectedly succeeded";
    } catch (const Error& error) {
        EXPECT_STREQ(error.what(), message);
    } catch (...) {
        ADD_FAILURE() << "Original exception type was replaced";
    }

    EXPECT_FALSE(worker.joinable());
    EXPECT_FALSE(ran);
}

TEST(AdapterWorkerStartup, PreservesPriorityAndMemoryLockExceptionsAndJoinsFailedWorker) {
    ExpectOriginalStartupFailure<utils::ThreadPriorityError>("priority denied");
    ExpectOriginalStartupFailure<utils::MemoryLockError>("memory lock denied");
    ExpectOriginalStartupFailure<std::logic_error>("non-platform initialization failure");
}

TEST(AdapterWorkerStartup, PreservesNonStandardInitializationException) {
    std::thread worker;
    EXPECT_THROW(adapter_worker::Start(
                     worker,
                     [] {
                         throw 42;
                     },
                     [] {}),
                 int);

    EXPECT_FALSE(worker.joinable());
}

TEST(AdapterStatisticsWait, StopBeforeWaitDoesNotLoseNotification) {
    StatisticsWait wait;
    std::atomic<bool> running{false};
    wait.Start(running);
    wait.Stop(running);

    auto result = std::async(std::launch::async, [&] {
        return wait.Wait(running);
    });
    EXPECT_EQ(result.wait_for(200ms), std::future_status::ready);
    EXPECT_FALSE(result.get());
}

TEST(AdapterStatisticsWait, StopWakesAllWaitersIncludingAlreadyStoppedFailurePaths) {
    StatisticsWait wait;
    std::atomic<bool> running{false};
    wait.Start(running);

    std::vector<std::future<bool>> results;
    for (unsigned i = 0; i < 4; ++i)
        results.push_back(std::async(std::launch::async, [&] {
            return wait.Wait(running);
        }));

    for (auto& result : results)
        EXPECT_EQ(result.wait_for(10ms), std::future_status::timeout);

    wait.Stop(running);
    wait.Stop(running);
    for (auto& result : results) {
        EXPECT_EQ(result.wait_for(200ms), std::future_status::ready);
        EXPECT_FALSE(result.get());
    }
}

TEST(AdapterStatisticsWait, RunningSamplerWaitsOneIntervalAndCanBeRestarted) {
    StatisticsWait wait;
    std::atomic<bool> running{false};
    wait.Start(running);

    const auto before = std::chrono::steady_clock::now();
    EXPECT_TRUE(wait.Wait(running));
    EXPECT_GE(std::chrono::steady_clock::now() - before, 400ms);
    wait.Stop(running);
    EXPECT_FALSE(wait.Wait(running));

    wait.Start(running);
    auto stopped = std::async(std::launch::async, [&] {
        return wait.Wait(running);
    });
    wait.Stop(running);
    EXPECT_EQ(stopped.wait_for(200ms), std::future_status::ready);
    EXPECT_FALSE(stopped.get());
}

}  // namespace

}  // namespace encos
