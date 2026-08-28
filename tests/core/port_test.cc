#include "utils/port.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

#include "motor/types.h"

namespace encos {
namespace {

static_assert(!std::is_copy_constructible_v<BasePort>);
static_assert(!std::is_copy_assignable_v<BasePort>);
static_assert(std::has_virtual_destructor_v<BasePort>);
static_assert(std::is_trivially_copyable_v<PortFrame>);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(alignof(Port<3>) >= 64U);

template <std::size_t Len, typename Message = PortFrame>
using InstrumentedPort = Port<Len, Message, detail::PortTestInstrumentation>;
static_assert(sizeof(Port<3>) < sizeof(InstrumentedPort<3>));

PortFrame MakeFrame(std::uint32_t value) {
    PortFrame frame{};
    frame.data[0] = static_cast<std::uint8_t>(value);
    frame.data[1] = static_cast<std::uint8_t>(value >> 8U);
    frame.data[2] = static_cast<std::uint8_t>(value >> 16U);
    frame.data[3] = static_cast<std::uint8_t>(value >> 24U);
    frame.data[4] = static_cast<std::uint8_t>(~value);
    frame.data[5] = static_cast<std::uint8_t>(~value >> 8U);
    frame.data[6] = static_cast<std::uint8_t>(~value >> 16U);
    frame.data[7] = static_cast<std::uint8_t>(~value >> 24U);
    frame.frame_flags = static_cast<std::uint8_t>(value & 0x0FU);
    frame.len = 8U;
    return frame;
}

std::uint32_t DecodeFrame(const PortFrame& frame) {
    return static_cast<std::uint32_t>(frame.data[0]) |
           (static_cast<std::uint32_t>(frame.data[1]) << 8U) |
           (static_cast<std::uint32_t>(frame.data[2]) << 16U) |
           (static_cast<std::uint32_t>(frame.data[3]) << 24U);
}

void ExpectCompleteFrame(const PortFrame& frame) {
    const std::uint32_t value = DecodeFrame(frame);
    EXPECT_EQ(frame.data[4], static_cast<std::uint8_t>(~value));
    EXPECT_EQ(frame.data[5], static_cast<std::uint8_t>(~value >> 8U));
    EXPECT_EQ(frame.data[6], static_cast<std::uint8_t>(~value >> 16U));
    EXPECT_EQ(frame.data[7], static_cast<std::uint8_t>(~value >> 24U));
    EXPECT_EQ(frame.frame_flags, static_cast<std::uint8_t>(value & 0x0FU));
    EXPECT_EQ(frame.len, 8U);
}

TEST(PortTests, StoresImmutableCanIdAndStartsEmpty) {
    InstrumentedPort<3> port(0x123U);
    const BasePort& base = port;

    EXPECT_EQ(base.GetCanId(), 0x123U);
    EXPECT_FALSE(port.Pop().has_value());
}

TEST(PortTests, ProducerAndConsumerHotStateUseDistinctCacheLines) {
    InstrumentedPort<3> port;
    using TestAccess = detail::PortTestAccess<3, PortFrame>;

    EXPECT_GE(TestAccess::SlotPublishedActiveAddress(port, 0U) -
                  TestAccess::SlotReaderHazardAddress(port, 0U),
              64U);
    EXPECT_GE(
        TestAccess::HeadPublishedActiveAddress(port) - TestAccess::HeadReaderHazardAddress(port),
        64U);
    EXPECT_GE(TestAccess::ConsumerSequenceAddress(port) - TestAccess::ProducerSequenceAddress(port),
              64U);
}

TEST(PortTests, EmptyTracksConsumerVisibleMessages) {
    InstrumentedPort<3> port;
    EXPECT_TRUE(port.Empty());

    port.Push(PortFrame{});
    EXPECT_FALSE(port.Empty());
    ASSERT_TRUE(port.Pop().has_value());
    EXPECT_TRUE(port.Empty());

    for (std::uint8_t value = 0; value < 5; ++value) {
        PortFrame frame{};
        frame.data[0] = value;
        port.Push(frame);
    }
    EXPECT_FALSE(port.Empty());

    port.Clear();
    EXPECT_TRUE(port.Empty());
}

TEST(PortTests, PreservesFifoOrderAndClearsUnreadMessages) {
    InstrumentedPort<3> port;
    PortFrame first{{1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U}, 0x03U, 8U};
    PortFrame second{{9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U}, 0x01U, 4U};

    port.Push(first);
    port.Push(second);

    const auto actual_first = port.Pop();
    ASSERT_TRUE(actual_first.has_value());
    EXPECT_EQ(*actual_first, first);
    port.Clear();
    EXPECT_FALSE(port.Pop().has_value());
}

TEST(PortTests, PreservesExactCapacityInFifoOrder) {
    InstrumentedPort<3> port;
    for (std::uint8_t value = 1; value <= 3; ++value) {
        PortFrame frame{};
        frame.data[0] = value;
        port.Push(frame);
    }

    for (std::uint8_t expected = 1; expected <= 3; ++expected) {
        const auto frame = port.Pop();
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->data[0], expected);
    }
}

TEST(PortTests, OverwritesOldestAndRetainsNewestCapacityMessages) {
    InstrumentedPort<3> port;
    for (std::uint8_t value = 1; value <= 5; ++value) {
        PortFrame frame{};
        frame.data[0] = value;
        port.Push(frame);
    }

    for (std::uint8_t expected = 3; expected <= 5; ++expected) {
        const auto frame = port.Pop();
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->data[0], expected);
    }
    EXPECT_FALSE(port.Pop().has_value());
}

TEST(PortTests, RetainsNewestMessagesAcrossMultipleFullWraps) {
    InstrumentedPort<5> port;
    for (std::uint8_t value = 0; value < 42; ++value) {
        PortFrame frame{};
        frame.data[0] = value;
        port.Push(frame);
    }

    for (std::uint8_t expected = 37; expected < 42; ++expected) {
        const auto frame = port.Pop();
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->data[0], expected);
    }
    EXPECT_FALSE(port.Pop().has_value());
}

TEST(PortTests, PreservesCustomMotorPackMessageFields) {
    InstrumentedPort<3, MotorPackMsg> port(BasePort::kAnyCanId);
    MotorPackMsg expected{0x18F0FFF2U, 0x07U, 8U, {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}};

    port.Push(expected);

    const auto actual = port.Pop();
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(*actual, expected);
}

TEST(PortTests, InvokesTypedCallbackSynchronouslyWithoutConsumingMessage) {
    std::thread::id callback_thread;
    std::size_t callback_count = 0;
    InstrumentedPort<3> port(BasePort::kAnyCanId, [&](const PortFrame&) {
        callback_thread = std::this_thread::get_id();
        ++callback_count;
    });
    const PortFrame frame{{42U}, 0U, 1U};

    port.Push(frame);

    EXPECT_EQ(callback_thread, std::this_thread::get_id());
    EXPECT_EQ(callback_count, 1U);
    EXPECT_TRUE(port.Pop().has_value());
}

struct HazardPause {
    static void PauseOnce(void* opaque) {
        auto& pause = *static_cast<HazardPause*>(opaque);
        std::unique_lock<std::mutex> lock(pause.mutex);
        if (pause.was_reached) {
            return;
        }
        pause.was_reached = true;
        pause.reached.notify_one();
        pause.resume.wait(lock, [&pause]() {
            return pause.may_resume;
        });
    }

    std::mutex mutex;
    std::condition_variable reached;
    std::condition_variable resume;
    bool was_reached{false};
    bool may_resume{false};
};

struct ValidationProgressProbe {
    static void PublishOneWrap(void* opaque) {
        auto& probe = *static_cast<ValidationProgressProbe*>(opaque);
        probe.validation_counts.push_back(TestAccess::ValidationFailureCount(*probe.port));
        probe.writer_steps.push_back(TestAccess::WriterStepCount(*probe.port));
        if (probe.wraps_remaining == 0U) {
            return;
        }

        --probe.wraps_remaining;
        for (std::size_t index = 0U; index < kCapacity; ++index) {
            probe.port->Push(MakeFrame(probe.next_value++));
        }
    }

    static constexpr std::size_t kCapacity = 3U;
    using TestPort = InstrumentedPort<kCapacity>;
    using TestAccess = detail::PortTestAccess<kCapacity, PortFrame>;

    TestPort* port;
    std::uint32_t wraps_remaining;
    std::uint32_t next_value;
    std::vector<std::uint64_t> validation_counts;
    std::vector<std::uint64_t> writer_steps;
};

TEST(PortProgressTests, SuspendedReaderNeverBlocksRepeatedWriterWraps) {
    InstrumentedPort<3> port;
    using TestAccess = detail::PortTestAccess<3, PortFrame>;
    port.Push(MakeFrame(0U));
    HazardPause pause;
    TestAccess::SetAfterHazardHook(port, &HazardPause::PauseOnce, &pause);

    auto reader = std::async(std::launch::async, [&port]() {
        return port.Pop();
    });
    {
        std::unique_lock<std::mutex> lock(pause.mutex);
        ASSERT_TRUE(pause.reached.wait_for(lock, std::chrono::seconds(2), [&pause]() {
            return pause.was_reached;
        }));
    }

    const std::uint64_t steps_before = TestAccess::WriterStepCount(port);
    constexpr std::uint32_t kPublishedWhilePaused = 3000U;
    auto writer = std::async(std::launch::async, [&port]() {
        for (std::uint32_t value = 1U; value <= kPublishedWhilePaused; ++value) {
            port.Push(MakeFrame(value));
        }
    });
    const bool writer_completed =
        writer.wait_for(std::chrono::seconds(2)) == std::future_status::ready;

    {
        std::lock_guard<std::mutex> lock(pause.mutex);
        pause.may_resume = true;
    }
    pause.resume.notify_one();
    writer.wait();

    EXPECT_TRUE(writer_completed);
    EXPECT_EQ(TestAccess::WriterStepCount(port) - steps_before,
              kPublishedWhilePaused * TestAccess::kWriterAtomicSteps);
    const auto value = reader.get();
    ASSERT_TRUE(value.has_value());
    ExpectCompleteFrame(*value);
    EXPECT_GE(DecodeFrame(*value), kPublishedWhilePaused - 2U);
    EXPECT_GT(TestAccess::ValidationFailureCount(port), 0U);
}

TEST(PortProgressTests, EveryValidationRetryHasMeasuredProducerProgress) {
    ValidationProgressProbe::TestPort port;
    port.Push(MakeFrame(0U));
    constexpr std::uint32_t kForcedRetries = 4U;
    ValidationProgressProbe probe{&port, kForcedRetries, 1U, {}, {}};
    ValidationProgressProbe::TestAccess::SetAfterHazardHook(
        port, &ValidationProgressProbe::PublishOneWrap, &probe);

    const auto message = port.Pop();

    ASSERT_TRUE(message.has_value());
    ASSERT_EQ(probe.validation_counts.size(), kForcedRetries + 1U);
    ASSERT_EQ(probe.writer_steps.size(), kForcedRetries + 1U);
    for (std::size_t retry = 1U; retry < probe.validation_counts.size(); ++retry) {
        EXPECT_EQ(probe.validation_counts[retry] - probe.validation_counts[retry - 1U], 1U);
        EXPECT_EQ(probe.writer_steps[retry] - probe.writer_steps[retry - 1U],
                  ValidationProgressProbe::kCapacity *
                      ValidationProgressProbe::TestAccess::kWriterAtomicSteps);
    }
}

TEST(PortProgressTests, PublicationCompletesBeforeArbitraryCallbackReturns) {
    std::mutex mutex;
    std::condition_variable entered;
    std::condition_variable resume;
    bool callback_entered = false;
    bool callback_may_return = false;
    std::atomic<bool> push_returned{false};
    InstrumentedPort<3> port(BasePort::kAnyCanId, [&](const PortFrame&) {
        std::unique_lock<std::mutex> lock(mutex);
        callback_entered = true;
        entered.notify_one();
        resume.wait(lock, [&callback_may_return]() {
            return callback_may_return;
        });
    });

    auto pusher = std::async(std::launch::async, [&] {
        port.Push(MakeFrame(42U));
        push_returned.store(true, std::memory_order_release);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(entered.wait_for(lock, std::chrono::seconds(2), [&callback_entered]() {
            return callback_entered;
        }));
    }

    const auto message = port.Pop();
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(DecodeFrame(*message), 42U);
    EXPECT_FALSE(push_returned.load(std::memory_order_acquire));

    {
        std::lock_guard<std::mutex> lock(mutex);
        callback_may_return = true;
    }
    resume.notify_one();
    EXPECT_EQ(pusher.wait_for(std::chrono::seconds(2)), std::future_status::ready);
}

TEST(PortProgressTests, ContinuousWriterRetainsNewestWindowWithoutReaderProgress) {
    InstrumentedPort<7> port;
    constexpr std::uint32_t kPublishCount = 100000U;
    for (std::uint32_t value = 0U; value < kPublishCount; ++value) {
        port.Push(MakeFrame(value));
    }

    for (std::uint32_t expected = kPublishCount - 7U; expected < kPublishCount; ++expected) {
        const auto message = port.Pop();
        ASSERT_TRUE(message.has_value());
        ExpectCompleteFrame(*message);
        EXPECT_EQ(DecodeFrame(*message), expected);
    }
    EXPECT_FALSE(port.Pop().has_value());
}

TEST(PortProgressTests, CrossesSequenceLowWordRolloverWithoutLosingFifoOrder) {
    InstrumentedPort<5> port;
    detail::PortTestAccess<5, PortFrame>::ResetEmptySequence(
        port, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) - 2U);
    for (std::uint32_t value = 0U; value < 6U; ++value) {
        port.Push(MakeFrame(value));
    }

    for (std::uint32_t expected = 1U; expected < 6U; ++expected) {
        const auto message = port.Pop();
        ASSERT_TRUE(message.has_value());
        EXPECT_EQ(DecodeFrame(*message), expected);
    }
    EXPECT_FALSE(port.Pop().has_value());
}

TEST(PortConcurrencyTests, ConcurrentOverwriteNeverReturnsTornOrDuplicateMessages) {
    InstrumentedPort<31> port;
    constexpr std::uint32_t kPublishCount = 200000U;
    std::atomic<bool> producer_done{false};
    std::thread producer([&port, &producer_done]() {
        for (std::uint32_t value = 0U; value < kPublishCount; ++value) {
            port.Push(MakeFrame(value));
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::optional<std::uint32_t> previous;
    while (!producer_done.load(std::memory_order_acquire)) {
        const auto message = port.Pop();
        if (!message.has_value()) {
            std::this_thread::yield();
            continue;
        }
        ExpectCompleteFrame(*message);
        const std::uint32_t value = DecodeFrame(*message);
        if (previous.has_value()) {
            EXPECT_GT(value, *previous);
        }
        previous = value;
    }
    producer.join();

    while (const auto message = port.Pop()) {
        ExpectCompleteFrame(*message);
        const std::uint32_t value = DecodeFrame(*message);
        if (previous.has_value()) {
            EXPECT_GT(value, *previous);
        }
        previous = value;
    }
    ASSERT_TRUE(previous.has_value());
    EXPECT_EQ(*previous, kPublishCount - 1U);
}

TEST(PortConcurrencyTests, ClearDuringPublicationNeverReturnsTornMessages) {
    InstrumentedPort<17> port;
    constexpr std::uint32_t kPublishCount = 50000U;
    std::atomic<bool> producer_done{false};
    std::thread producer([&port, &producer_done]() {
        for (std::uint32_t value = 0U; value < kPublishCount; ++value) {
            port.Push(MakeFrame(value));
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::uint32_t iterations = 0U;
    while (!producer_done.load(std::memory_order_acquire)) {
        if ((iterations++ % 11U) == 0U) {
            port.Clear();
            continue;
        }
        const auto message = port.Pop();
        if (message.has_value()) {
            ExpectCompleteFrame(*message);
        }
    }
    producer.join();
    while (const auto message = port.Pop()) {
        ExpectCompleteFrame(*message);
    }
}

TEST(PortLivenessRegressionTests, PopCompletesWhenProducerPausesAfterOddSlotStore) {
    InstrumentedPort<3> port;
    using TestAccess = detail::PortTestAccess<3, PortFrame>;
    port.Push(MakeFrame(1U));
    port.Push(MakeFrame(2U));
    port.Push(MakeFrame(3U));
    HazardPause pause;
    TestAccess::SetAfterSlotOddStoreHook(port, &HazardPause::PauseOnce, &pause);

    auto writer = std::async(std::launch::async, [&port]() {
        port.Push(MakeFrame(4U));
    });
    {
        std::unique_lock<std::mutex> lock(pause.mutex);
        ASSERT_TRUE(pause.reached.wait_for(lock, std::chrono::seconds(2), [&pause]() {
            return pause.was_reached;
        }));
    }

    const auto message = port.Pop();
    {
        std::lock_guard<std::mutex> lock(pause.mutex);
        pause.may_resume = true;
    }
    pause.resume.notify_one();

    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(DecodeFrame(*message), 1U);
    EXPECT_EQ(writer.wait_for(std::chrono::seconds(2)), std::future_status::ready);
}

TEST(PortLivenessRegressionTests, PopResynchronizesWithinSameCallWhenSlotLeadsHead) {
    InstrumentedPort<3> port;
    using TestAccess = detail::PortTestAccess<3, PortFrame>;
    port.Push(MakeFrame(1U));
    port.Push(MakeFrame(2U));
    port.Push(MakeFrame(3U));
    HazardPause pause;
    TestAccess::SetAfterSlotActiveStoreHook(port, &HazardPause::PauseOnce, &pause);
    const std::uint64_t validation_before = TestAccess::ValidationFailureCount(port);
    const std::uint64_t steps_before = TestAccess::WriterStepCount(port);

    auto writer = std::async(std::launch::async, [&port]() {
        port.Push(MakeFrame(4U));
    });
    {
        std::unique_lock<std::mutex> lock(pause.mutex);
        ASSERT_TRUE(pause.reached.wait_for(lock, std::chrono::seconds(2), [&pause]() {
            return pause.was_reached;
        }));
    }

    const auto message = port.Pop();
    EXPECT_EQ(TestAccess::ValidationFailureCount(port) - validation_before, 1U);
    EXPECT_EQ(TestAccess::WriterStepCount(port) - steps_before,
              TestAccess::kSlotPublicationAtomicSteps);
    {
        std::lock_guard<std::mutex> lock(pause.mutex);
        pause.may_resume = true;
    }
    pause.resume.notify_one();

    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(DecodeFrame(*message), 2U);
    EXPECT_EQ(writer.wait_for(std::chrono::seconds(2)), std::future_status::ready);
}

TEST(PortLivenessRegressionTests, ClearCompletesWhenProducerPausesAfterOddHeadStore) {
    InstrumentedPort<3> port;
    using TestAccess = detail::PortTestAccess<3, PortFrame>;
    port.Push(MakeFrame(1U));
    HazardPause pause;
    TestAccess::SetAfterHeadOddStoreHook(port, &HazardPause::PauseOnce, &pause);

    auto writer = std::async(std::launch::async, [&port]() {
        port.Push(MakeFrame(2U));
    });
    {
        std::unique_lock<std::mutex> lock(pause.mutex);
        ASSERT_TRUE(pause.reached.wait_for(lock, std::chrono::seconds(2), [&pause]() {
            return pause.was_reached;
        }));
    }

    port.Clear();
    {
        std::lock_guard<std::mutex> lock(pause.mutex);
        pause.may_resume = true;
    }
    pause.resume.notify_one();

    EXPECT_EQ(writer.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    const auto message = port.Pop();
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(DecodeFrame(*message), 2U);
}

TEST(PortLivenessRegressionTests, SlotSnapshotSurvivesVersionAndActiveIndexAba) {
    InstrumentedPort<3> port;
    using TestAccess = detail::PortTestAccess<3, PortFrame>;
    port.Push(MakeFrame(0U));
    TestAccess::ForcePublicationVersionsNearWrap(port);
    HazardPause pause;
    TestAccess::SetBeforeSlotHazardHook(port, &HazardPause::PauseOnce, &pause);

    auto reader = std::async(std::launch::async, [&port]() {
        return port.Pop();
    });
    {
        std::unique_lock<std::mutex> lock(pause.mutex);
        ASSERT_TRUE(pause.reached.wait_for(lock, std::chrono::seconds(2), [&pause]() {
            return pause.was_reached;
        }));
    }

    for (std::uint32_t value = 1U; value <= 9U; ++value) {
        port.Push(MakeFrame(value));
    }
    {
        std::lock_guard<std::mutex> lock(pause.mutex);
        pause.may_resume = true;
    }
    pause.resume.notify_one();

    const auto message = reader.get();
    ASSERT_TRUE(message.has_value());
    ExpectCompleteFrame(*message);
    EXPECT_GE(DecodeFrame(*message), 7U);
    EXPECT_GT(TestAccess::ValidationFailureCount(port), 0U);
}

TEST(PortLivenessRegressionTests, HeadSnapshotSurvivesVersionAndActiveIndexAba) {
    InstrumentedPort<3> port;
    using TestAccess = detail::PortTestAccess<3, PortFrame>;
    TestAccess::ResetEmptySequence(
        port, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) - 1U);
    port.Push(MakeFrame(0U));
    TestAccess::ForcePublicationVersionsNearWrap(port);
    HazardPause pause;
    TestAccess::SetBeforeHeadHazardHook(port, &HazardPause::PauseOnce, &pause);

    auto clearer = std::async(std::launch::async, [&port]() {
        port.Clear();
    });
    {
        std::unique_lock<std::mutex> lock(pause.mutex);
        ASSERT_TRUE(pause.reached.wait_for(lock, std::chrono::seconds(2), [&pause]() {
            return pause.was_reached;
        }));
    }

    for (std::uint32_t value = 1U; value <= 9U; ++value) {
        port.Push(MakeFrame(value));
    }
    {
        std::lock_guard<std::mutex> lock(pause.mutex);
        pause.may_resume = true;
    }
    pause.resume.notify_one();
    clearer.get();

    EXPECT_FALSE(port.Pop().has_value());
    port.Push(MakeFrame(10U));
    const auto message = port.Pop();
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(DecodeFrame(*message), 10U);
}

}  // namespace
}  // namespace encos
