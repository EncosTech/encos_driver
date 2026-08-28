#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

#include "driver_manager_test_access.h"
#include "motor/waiter_test_access.h"
#include "test_fixtures.h"

namespace encos {

class ResponseWaiterTests : public MotorTestFixture {};

bool WaitForCommandCount(FakeAdapter* adapter, std::size_t expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < deadline) {
        if (adapter->GetCommandRecords().size() == expected) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

TEST_F(ResponseWaiterTests, RegistersBeforeSynchronousFeedback) {
    const auto feedback = motor->SpdControl<1>(1.0f, 1.0f);

    EXPECT_EQ(feedback.error, MotorError::NoError);
}

TEST_F(ResponseWaiterTests, MatchingWaiterCompletesBeforeBlockingStatusCallback) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    std::mutex mutex;
    std::condition_variable condition;
    bool callback_entered = false;
    bool release_callback = false;
    motor->SetOnStatus([&](const MotorStatus&) {
        std::unique_lock<std::mutex> lock(mutex);
        callback_entered = true;
        condition.notify_all();
        condition.wait(lock, [&] {
            return release_callback;
        });
    });

    auto control = std::async(std::launch::async, [&] {
        return motor->SpdControl<1>(1.0F, 1.0F);
    });
    ASSERT_TRUE(WaitForCommandCount(adapter, 1));

    MotorStatus status{};
    status.error = MotorError::NoError;
    status.speed = 1.0F;
    status.current = 0.5F;
    auto receive = std::async(std::launch::async, [&] {
        adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status));
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        EXPECT_TRUE(condition.wait_for(lock, std::chrono::seconds(1), [&] {
            return callback_entered;
        }));
    }

    EXPECT_EQ(control.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_callback = true;
    }
    condition.notify_all();
    receive.get();
    EXPECT_EQ(control.get().error, MotorError::NoError);
}

TEST_F(ResponseWaiterTests, SameMotorTransactionsKeepResponsesIsolated) {
    adapter->SetReplyMode(FakeReplyMode::Manual);

    auto first = std::async(std::launch::async, [&] {
        return motor->SpdControl<1>(1.0F, 1.0F);
    });
    if (!WaitForCommandCount(adapter, 1)) {
        ADD_FAILURE() << "first command was not sent";
        (void) EncosDriverManager::Instance().DestroyMotor(motor);
        return;
    }

    auto second = std::async(std::launch::async, [&] {
        return motor->SpdControl<1>(2.0F, 1.0F);
    });
    EXPECT_TRUE(MotorWaiterTestAccess::TransactionMutexIsLocked(motor));
    EXPECT_EQ(adapter->GetCommandRecords().size(), 1U);

    MotorStatus first_status{};
    first_status.error = MotorError::NoError;
    first_status.speed = 1.0F;
    first_status.current = 0.5F;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, first_status));
    ASSERT_EQ(first.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    if (!WaitForCommandCount(adapter, 2)) {
        ADD_FAILURE() << "second command was not sent";
        (void) EncosDriverManager::Instance().DestroyMotor(motor);
        return;
    }

    MotorStatus second_status{};
    second_status.error = MotorError::NoError;
    second_status.speed = 2.0F;
    second_status.current = 0.75F;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, second_status));

    const auto first_feedback = first.get();
    const auto second_feedback = second.get();
    EXPECT_NEAR(first_feedback.speed, first_status.speed, kDecodedFloatTolerance);
    EXPECT_NEAR(first_feedback.current, first_status.current, kDecodedFloatTolerance);
    EXPECT_NEAR(second_feedback.speed, second_status.speed, kDecodedFloatTolerance);
    EXPECT_NEAR(second_feedback.current, second_status.current, kDecodedFloatTolerance);
}

TEST_F(ResponseWaiterTests, FirmwareInitializationRegistersRouteBeforeSynchronousReplies) {
    adapter->SeedMotor(0, 2, MotorModel::EC_A4310_P2);

    auto* initialized = bus->GetMotor(2);

    ASSERT_NE(initialized, nullptr);
    EXPECT_EQ(EncosDriverManager::Instance().FindMotor(bus, 2), initialized);
}

TEST_F(ResponseWaiterTests, ExplicitCanFdKnownModelOverloadUpdatesReusedMotor) {
    auto* initialized = bus->GetMotor(2, MotorModel::EC_A4310_P2, true);

    ASSERT_NE(initialized, nullptr);
    EXPECT_TRUE(initialized->IsCanFdEnabled());

    auto* reused = bus->GetMotor(2, MotorModel::EC_A4310_P2, false);
    EXPECT_EQ(reused, initialized);
    EXPECT_FALSE(reused->IsCanFdEnabled());
}

TEST_F(ResponseWaiterTests, ExplicitCanFdConstructorOverloadsPreserveOtherFrameFlags) {
    const auto model_motor = MotorWaiterTestAccess::CreateWithModel(
        bus, MotorModel::EC_A4310_P2, static_cast<uint8_t>(kCanFrameFlagEff | kCanFrameFlagFdMask),
        false);
    EXPECT_EQ(MotorWaiterTestAccess::FrameFlags(model_motor.get()), kCanFrameFlagEff);

    const auto ranges_motor = MotorWaiterTestAccess::CreateWithRanges(
        bus, GetMotorModelRanges(MotorModel::EC_A4310_P2), kCanFrameFlagEff, true);
    EXPECT_EQ(MotorWaiterTestAccess::FrameFlags(ranges_motor.get()),
              static_cast<uint8_t>(kCanFrameFlagEff | kCanFrameFlagFdMask));

    const auto firmware_motor =
        MotorWaiterTestAccess::CreateForFirmwareInitialization(bus, 0, true);
    EXPECT_EQ(MotorWaiterTestAccess::FrameFlags(firmware_motor.get()), kCanFrameFlagFdMask);
}

TEST_F(ResponseWaiterTests, ExplicitCanFdRangesOverloadPreservesRanges) {
    auto ranges = GetMotorModelRanges(MotorModel::EC_A4310_P2);
    ranges.current.max = 37.5F;

    auto* initialized = bus->GetMotor(3, ranges, true);

    ASSERT_NE(initialized, nullptr);
    EXPECT_TRUE(initialized->IsCanFdEnabled());
    EXPECT_FLOAT_EQ(initialized->GetPVTRanges().current.max, 37.5F);
}

TEST_F(ResponseWaiterTests, ExplicitCanFdFirmwareOverloadUsesProtocolBeforeInitialization) {
    adapter->SeedMotor(0, 4, MotorModel::EC_A4310_P2);
    adapter->ClearCommandRecords();

    auto* initialized = bus->GetMotor(4, true);

    ASSERT_NE(initialized, nullptr);
    EXPECT_TRUE(initialized->IsCanFdEnabled());
    const auto commands = adapter->GetCommandRecords();
    ASSERT_FALSE(commands.empty());
    for (const auto& command : commands) {
        EXPECT_EQ(command.kind, FakeCommandKind::GetParameter);
        EXPECT_TRUE(CanFrameFlagsUseCanFd(command.raw_frame_flags));
    }

    auto* reused = bus->GetMotor(4, false);
    EXPECT_EQ(reused, initialized);
    EXPECT_FALSE(reused->IsCanFdEnabled());
}

TEST_F(ResponseWaiterTests, DiscoveryProtocolOverridesOtherFlagsBeforeRangeInitialization) {
    adapter->SeedMotor(0, 5, MotorModel::EC_A4310_P2);
    adapter->ClearCommandRecords();

    auto* initialized = EncosDriverManager::Instance().CreateMotor(bus, 5, kCanFrameFlagEff, true);

    ASSERT_NE(initialized, nullptr);
    EXPECT_TRUE(initialized->IsCanEffEnabled());
    EXPECT_TRUE(initialized->IsCanFdEnabled());
    const auto commands = adapter->GetCommandRecords();
    ASSERT_FALSE(commands.empty());
    for (const auto& command : commands) {
        EXPECT_EQ(command.kind, FakeCommandKind::GetParameter);
        EXPECT_TRUE(CanFrameFlagsUseExtendedId(command.raw_frame_flags));
        EXPECT_TRUE(CanFrameFlagsUseCanFd(command.raw_frame_flags));
    }
}

TEST_F(ResponseWaiterTests, FailedFirmwareInitializationRollsBackRouteAndPendingObject) {
    adapter->SeedMotor(0, 2, MotorModel::EC_A4310_P2);
    adapter->SetReplyMode(FakeReplyMode::Manual);

    EXPECT_THROW((void) bus->GetMotor(2), std::runtime_error);
    EXPECT_EQ(EncosDriverManager::Instance().FindMotor(bus, 2), nullptr);

    adapter->SetReplyMode(FakeReplyMode::Automatic);
    EXPECT_NE(bus->GetMotor(2), nullptr);
}

TEST_F(ResponseWaiterTests, InitializerFailureWaitsForInFlightRouteCallback) {
    adapter->SeedMotor(0, 2, MotorModel::EC_A4310_P2);
    adapter->SetReplyMode(FakeReplyMode::Manual);
    std::mutex mutex;
    std::condition_variable condition;
    bool waiter_registered = false;
    bool callback_entered = false;
    bool release_callback = false;
    std::thread command;
    std::thread receive;
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::SetDeviceInitializerHook(manager, [&](void* device) {
        auto* initializing_motor = static_cast<Motor*>(device);
        MotorWaiterTestAccess::SetHooks(
            initializing_motor,
            [&] {
                std::lock_guard<std::mutex> lock(mutex);
                waiter_registered = true;
                condition.notify_all();
            },
            [&] {
                std::unique_lock<std::mutex> lock(mutex);
                callback_entered = true;
                condition.notify_all();
                condition.wait(lock, [&] {
                    return release_callback;
                });
            },
            {});
        command = std::thread([&] {
            (void) initializing_motor->SpdControl<1>(1.0f, 1.0f);
        });
        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [&] {
                return waiter_registered;
            });
        }
        receive = std::thread([&] {
            MotorPackMsg message{};
            message.id = 2;
            adapter->InjectMessage(MotorMessage{0, message});
        });
        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [&] {
                return callback_entered;
            });
        }
        throw std::runtime_error("initializer failed after route activation");
    });

    std::atomic<bool> creation_returned{false};
    auto creation = std::async(std::launch::async, [&] {
        EXPECT_THROW((void) bus->GetMotor(2), std::runtime_error);
        creation_returned.store(true, std::memory_order_release);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] {
            return callback_entered;
        });
    }
    EXPECT_FALSE(creation_returned.load(std::memory_order_acquire));
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_callback = true;
    }
    condition.notify_all();
    receive.join();
    command.join();
    creation.get();
    DriverManagerTestAccess::SetDeviceInitializerHook(manager, {});
    EXPECT_EQ(manager.FindMotor(bus, 2), nullptr);
}

TEST_F(ResponseWaiterTests, SharedSystemResponseBroadcastsToEveryMotorWaiter) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    adapter->SeedMotor(0, 2, MotorModel::EC_A4310_P2);
    auto* other_motor = bus->GetMotor(2, MotorModel::EC_A4310_P2);
    ASSERT_NE(other_motor, nullptr);

    std::atomic<bool> first_done{false};
    std::atomic<bool> second_done{false};
    bool first_result = false;
    bool second_result = true;
    std::thread first([&] {
        first_result = motor->SetPos(0.1);
        first_done.store(true);
    });
    std::thread second([&] {
        second_result = other_motor->SetPos(0.2);
        second_done.store(true);
    });
    while (adapter->GetCommandRecords().size() < 2) {
        std::this_thread::yield();
    }

    MotorPackMsg ack{};
    ack.id = 0x7FF;
    ack.len = 4;
    ack.data[0] = 0;
    ack.data[1] = 1;
    ack.data[3] = 0x03;
    adapter->InjectMessage(MotorMessage{0, ack});

    first.join();
    second.join();
    EXPECT_TRUE(first_done.load());
    EXPECT_TRUE(second_done.load());
    EXPECT_TRUE(first_result);
    EXPECT_FALSE(second_result);
}

TEST_F(ResponseWaiterTests, SharedSystemResponseRemainsInBusUnknownMailbox) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    bool reset_result = false;
    std::thread reset([&] {
        reset_result = bus->ResetMotorsId(true);
    });
    while (adapter->GetCommandRecords().empty()) {
        std::this_thread::yield();
    }

    MotorPackMsg response{};
    response.id = 0x7FF;
    response.len = 2;
    response.data[0] = 0x7F;
    response.data[1] = 0x7F;
    adapter->InjectMessage(MotorMessage{0, response});

    reset.join();
    EXPECT_TRUE(reset_result);
}

TEST_F(ResponseWaiterTests, ResetMotorsIdRetiresEveryMotorExceptExistingIdOne) {
    auto* second = bus->GetMotor(2, MotorModel::EC_A4310_P2);
    auto* third = bus->GetMotor(3, MotorModel::EC_A4310_P2);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(third, nullptr);
    adapter->SetReplyMode(FakeReplyMode::Manual);

    bool reset_result = false;
    std::thread reset([&] {
        reset_result = bus->ResetMotorsId(true);
    });
    while (adapter->GetCommandRecords().empty()) {
        std::this_thread::yield();
    }
    MotorPackMsg ack{};
    ack.id = 0x7FF;
    ack.len = 2;
    ack.data[0] = 0x7F;
    ack.data[1] = 0x7F;
    adapter->InjectMessage(MotorMessage{0, ack});
    reset.join();

    EXPECT_TRUE(reset_result);
    const auto motors = bus->GetMotors();
    ASSERT_EQ(motors.size(), 1u);
    EXPECT_EQ(motors.at(1), motor);
    EXPECT_EQ(bus->SelectMotor(2), nullptr);
    EXPECT_EQ(bus->SelectMotor(3), nullptr);
    MotorPackMsg stale_route{};
    stale_route.id = 2;
    EXPECT_FALSE(EncosDriverManager::Instance().DispatchReceive(adapter, 0, stale_route));
}

TEST_F(ResponseWaiterTests, ResetMotorsIdMigratesTheOnlyRemainingMotorToIdOne) {
    ASSERT_TRUE(EncosDriverManager::Instance().DestroyMotor(motor));
    motor = bus->GetMotor(2, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    adapter->SetReplyMode(FakeReplyMode::Manual);

    bool reset_result = false;
    std::thread reset([&] {
        reset_result = bus->ResetMotorsId(true);
    });
    while (adapter->GetCommandRecords().empty()) {
        std::this_thread::yield();
    }
    MotorPackMsg ack{};
    ack.id = 0x7FF;
    ack.len = 2;
    ack.data[0] = 0x7F;
    ack.data[1] = 0x7F;
    adapter->InjectMessage(MotorMessage{0, ack});
    reset.join();

    EXPECT_TRUE(reset_result);
    EXPECT_EQ(bus->SelectMotor(1), motor);
    EXPECT_EQ(bus->SelectMotor(2), nullptr);
}

TEST_F(ResponseWaiterTests, ResetMotorsIdTimeoutLeavesManagedMotorsUnchanged) {
    auto* second = bus->GetMotor(2, MotorModel::EC_A4310_P2);
    ASSERT_NE(second, nullptr);
    adapter->SetReplyMode(FakeReplyMode::Manual);

    EXPECT_FALSE(bus->ResetMotorsId(true));
    EXPECT_EQ(bus->SelectMotor(1), motor);
    EXPECT_EQ(bus->SelectMotor(2), second);
}

TEST_F(ResponseWaiterTests, ResetMotorsIdRejectsCreationUntilRetirementCompletes) {
    auto* second = bus->GetMotor(2, MotorModel::EC_A4310_P2);
    ASSERT_NE(second, nullptr);
    adapter->SetReplyMode(FakeReplyMode::Manual);
    std::mutex mutex;
    std::condition_variable condition;
    bool deleting = false;
    bool release_delete = false;
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::SetDeletionHook(manager, [&](EncosDriverManager::DeletionStage stage) {
        if (stage != EncosDriverManager::DeletionStage::BeforeDeviceDestroy) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex);
        deleting = true;
        condition.notify_all();
        condition.wait(lock, [&] {
            return release_delete;
        });
    });

    bool reset_result = false;
    std::thread reset([&] {
        reset_result = bus->ResetMotorsId(true);
    });
    while (adapter->GetCommandRecords().empty()) {
        std::this_thread::yield();
    }
    MotorPackMsg ack{};
    ack.id = 0x7FF;
    ack.len = 2;
    ack.data[0] = 0x7F;
    ack.data[1] = 0x7F;
    adapter->InjectMessage(MotorMessage{0, ack});
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] {
            return deleting;
        });
    }
    EXPECT_THROW((void) bus->GetMotor(3, MotorModel::EC_A4310_P2), std::invalid_argument);
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_delete = true;
    }
    condition.notify_all();
    reset.join();
    DriverManagerTestAccess::SetDeletionHook(manager, {});

    EXPECT_TRUE(reset_result);
    EXPECT_EQ(bus->SelectMotor(3), nullptr);
}

TEST_F(ResponseWaiterTests, ResetMotorsIdMigrationFailurePreservesSurvivorAndReportsFailure) {
    auto& manager = EncosDriverManager::Instance();
    ASSERT_TRUE(manager.DestroyMotor(motor));
    motor = bus->GetMotor(2, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);
    adapter->SetReplyMode(FakeReplyMode::Manual);
    DriverManagerTestAccess::SetMigrationHook(manager, [](EncosDriverManager::MigrationStage) {
        throw std::runtime_error("injected migration failure");
    });

    auto reset = std::async(std::launch::async, [this] {
        return bus->ResetMotorsId(true);
    });
    while (adapter->GetCommandRecords().empty()) {
        std::this_thread::yield();
    }
    MotorPackMsg ack{};
    ack.id = 0x7FF;
    ack.len = 2;
    ack.data[0] = 0x7F;
    ack.data[1] = 0x7F;
    adapter->InjectMessage(MotorMessage{0, ack});

    EXPECT_EQ(reset.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    DriverManagerTestAccess::SetMigrationHook(manager, {});
    EXPECT_FALSE(reset.get());
    EXPECT_EQ(bus->SelectMotor(2), motor);
    EXPECT_EQ(bus->SelectMotor(1), nullptr);
}

TEST_F(ResponseWaiterTests, RetiredMotorRejectsEveryPublicStatusConfigurationSetter) {
    auto& manager = EncosDriverManager::Instance();
    std::mutex mutex;
    std::condition_variable condition;
    bool retiring = false;
    bool release_destroy = false;
    DriverManagerTestAccess::SetDeletionHook(manager, [&](EncosDriverManager::DeletionStage stage) {
        if (stage != EncosDriverManager::DeletionStage::BeforeDeviceDestroy) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex);
        retiring = true;
        condition.notify_all();
        condition.wait(lock, [&] {
            return release_destroy;
        });
    });

    auto destruction = std::async(std::launch::async, [&] {
        return manager.DestroyMotor(motor);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] {
            return retiring;
        });
    }

    EXPECT_THROW(motor->SetMaxStatusLifeCycle(1), std::runtime_error);
    EXPECT_THROW(motor->SetStatusMedianFilterWindowSize(3), std::runtime_error);
    EXPECT_THROW(motor->SetStatusLimitFilterMaxDeltas(1.0f, 2.0f, 3.0f, 4.0f, 5.0f),
                 std::runtime_error);
    EXPECT_THROW(motor->SetStatusLimitFilterMaxDeltas(MotorStatus{}), std::runtime_error);

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_destroy = true;
    }
    condition.notify_all();
    EXPECT_TRUE(destruction.get());
    DriverManagerTestAccess::SetDeletionHook(manager, {});
    motor = nullptr;
}

TEST_F(ResponseWaiterTests, DestroyMotorCancelsAndWakesPendingWaiter) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    std::mutex mutex;
    std::condition_variable condition;
    bool waiter_registered = false;
    bool release_waiter = false;
    bool cancellation_started = false;
    std::atomic<bool> destroyed{false};
    MotorWaiterTestAccess::SetHooks(motor,
                                    [&] {
                                        std::unique_lock<std::mutex> lock(mutex);
                                        waiter_registered = true;
                                        condition.notify_all();
                                        condition.wait(lock, [&] {
                                            return release_waiter;
                                        });
                                    },
                                    {}, {});
    MotorWaiterTestAccess::SetCancellationHook(motor, [&] {
        std::lock_guard<std::mutex> lock(mutex);
        cancellation_started = true;
        condition.notify_all();
    });
    MotorFeedbackMsg1 feedback{};
    std::thread command([&] {
        feedback = motor->SpdControl<1>(1.0f, 1.0f);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] {
            return waiter_registered;
        });
    }

    std::thread destroy([&] {
        destroyed.store(EncosDriverManager::Instance().DestroyMotor(motor));
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] {
            return cancellation_started;
        });
    }
    EXPECT_FALSE(destroyed.load());
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_waiter = true;
    }
    condition.notify_all();
    motor = nullptr;
    command.join();
    destroy.join();
    EXPECT_TRUE(destroyed.load());
    EXPECT_EQ(feedback.error, MotorError::NoResponse);
}

TEST_F(ResponseWaiterTests, SpuriousWakeDoesNotCompleteWaiterPredicate) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    std::atomic<bool> registered{false};
    MotorWaiterTestAccess::SetHooks(motor,
                                    [&] {
                                        registered.store(true);
                                    },
                                    {}, {});
    MotorFeedbackMsg1 feedback{};
    std::thread command([&] {
        feedback = motor->SpdControl<1>(1.0f, 1.0f);
    });
    while (!registered.load()) {
        std::this_thread::yield();
    }

    MotorWaiterTestAccess::NotifyAll(motor);
    command.join();
    EXPECT_EQ(feedback.error, MotorError::NoResponse);
    MotorWaiterTestAccess::SetHooks(motor, {}, {}, {});
}

TEST_F(ResponseWaiterTests, TimeoutUnregistersWaiterBeforeLateFrameCanWrite) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    std::mutex mutex;
    std::condition_variable condition;
    bool registered = false;
    std::atomic<unsigned> writer_calls{0};
    MotorWaiterTestAccess::SetHooks(
        motor,
        [&] {
            std::lock_guard<std::mutex> lock(mutex);
            registered = true;
            condition.notify_all();
        },
        {},
        [&] {
            writer_calls.fetch_add(1, std::memory_order_relaxed);
        });
    MotorPackMsg request{};
    request.id = 1;
    std::optional<MotorPackMsg> result;
    std::thread waiter([&] {
        result = MotorWaiterTestAccess::WaitForPacket(
            motor, request,
            [](const MotorPackMsg& message) {
                return message.id == 1;
            },
            std::chrono::milliseconds(20));
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] {
            return registered;
        });
    }
    waiter.join();
    EXPECT_FALSE(result.has_value());

    MotorPackMsg late{};
    late.id = 1;
    late.len = 8;
    late.data[0] = 1u << 5u;
    late.data[6] = 50;
    late.data[7] = 50;
    adapter->InjectMessage(MotorMessage{0, late});
    EXPECT_EQ(writer_calls.load(std::memory_order_relaxed), 0u);
    MotorWaiterTestAccess::SetHooks(motor, {}, {}, {});
}

TEST_F(ResponseWaiterTests, MatchingStatusCompletesAllTypedWaitersAndUpdatesStatus) {
    adapter->SetReplyMode(FakeReplyMode::Manual);
    std::mutex mutex;
    std::condition_variable condition;
    unsigned registrations = 0;
    bool release_first_registration = false;
    MotorWaiterTestAccess::SetHooks(motor,
                                    [&] {
                                        std::unique_lock<std::mutex> lock(mutex);
                                        ++registrations;
                                        condition.notify_all();
                                        if (registrations == 1) {
                                            condition.wait(lock, [&] {
                                                return release_first_registration;
                                            });
                                        }
                                    },
                                    {}, {});
    MotorPackMsg request{};
    request.id = 1;
    std::optional<MotorPackMsg> packet_result;
    std::optional<bool> boolean_result;
    const auto checker = [](const MotorPackMsg& message) {
        return message.id == 1;
    };
    std::thread packet_waiter([&] {
        packet_result = MotorWaiterTestAccess::WaitForPacket(motor, request, checker,
                                                             std::chrono::milliseconds(200));
    });
    std::thread boolean_waiter([&] {
        boolean_result = MotorWaiterTestAccess::WaitForBoolean(motor, request, checker,
                                                               std::chrono::milliseconds(200));
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] {
            return registrations == 2;
        });
    }
    const bool first_command_sent = WaitForCommandCount(adapter, 1);
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_first_registration = true;
    }
    condition.notify_all();
    const bool both_commands_sent = WaitForCommandCount(adapter, 2);

    MotorPackMsg status{};
    status.id = 1;
    status.len = 8;
    status.data[0] = 1u << 5u;
    status.data[6] = 50;
    status.data[7] = 50;
    adapter->InjectMessage(MotorMessage{0, status});

    packet_waiter.join();
    boolean_waiter.join();
    EXPECT_TRUE(first_command_sent);
    EXPECT_TRUE(both_commands_sent);
    ASSERT_TRUE(packet_result.has_value());
    EXPECT_EQ(*packet_result, status);
    ASSERT_TRUE(boolean_result.has_value());
    EXPECT_TRUE(*boolean_result);
    const auto motor_status = motor->GetStatus();
    ASSERT_TRUE(motor_status.has_value());
    EXPECT_EQ(motor_status->error, MotorError::NoError);
    MotorWaiterTestAccess::SetHooks(motor, {}, {}, {});
}

TEST_F(ResponseWaiterTests, CheckerOrWriterExceptionCancelsOnlyThatWaiter) {
    MotorWaiterTestAccess::SetHooks(motor, {},
                                    [] {
                                        throw std::runtime_error("checker");
                                    },
                                    {});
    EXPECT_EQ(motor->SpdControl<1>(1.0f, 1.0f).error, MotorError::NoResponse);

    MotorWaiterTestAccess::SetHooks(motor, {}, {}, [] {
        throw std::runtime_error("writer");
    });
    EXPECT_EQ(motor->SpdControl<1>(1.0f, 1.0f).error, MotorError::NoResponse);
    MotorWaiterTestAccess::SetHooks(motor, {}, {}, {});
}

}  // namespace encos
