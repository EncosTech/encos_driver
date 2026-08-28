#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "bus/bus.h"
#include "driver_manager_test_access.h"
#include "encos/driver_manager.h"
#include "encos/encos_driver.h"
#include "glove/glove.h"
#include "test_fixtures.h"

namespace encos {

namespace {

constexpr uint8_t kGloveFingerCount = 5;
constexpr uint8_t kGloveEncodersPerFinger = 10;
constexpr float kTwoPi = 6.28318530717958647692F;

uint32_t GloveEncoderId(uint8_t finger_idx, uint8_t encoder_idx) {
    return 0x8100u + static_cast<uint32_t>(finger_idx) * kGloveEncodersPerFinger + encoder_idx;
}

MotorMessage MakeGloveEncoderFrame(uint8_t finger_idx, uint8_t encoder_idx, uint16_t raw_angle,
                                   bool online = true) {
    MotorMessage message{};
    message.bus_idx = finger_idx;
    message.data.id = GloveEncoderId(finger_idx, encoder_idx);
    message.data.frame_flags = kCanFrameFlagEff;
    message.data.len = 3;
    message.data.data[0] = static_cast<uint8_t>(raw_angle & 0xFFu);
    message.data.data[1] = static_cast<uint8_t>(raw_angle >> 8u);
    message.data.data[2] = online ? 1u : 0u;
    return message;
}

MotorMessage MakeGloveCalibrationResponse(uint8_t finger_idx, uint8_t result) {
    MotorMessage message{};
    message.bus_idx = finger_idx;
    message.data.id = 0x8150u;
    message.data.frame_flags = kCanFrameFlagEff;
    message.data.len = 8;
    message.data.data[0] = result;
    return message;
}

bool HasCompleteSnapshot(const GloveStatus& status) {
    for (const auto& finger : status) {
        for (const auto& encoder : finger) {
            if (!encoder.has_value()) {
                return false;
            }
        }
    }
    return true;
}

bool HasOnlyMissingEncoder(const GloveStatus& status, uint8_t missing_finger,
                           uint8_t missing_encoder) {
    for (uint8_t finger = 0; finger < kGloveFingerCount; ++finger) {
        for (uint8_t encoder = 0; encoder < kGloveEncodersPerFinger; ++encoder) {
            const bool expected_missing = finger == missing_finger && encoder == missing_encoder;
            if (status[finger][encoder].has_value() == expected_missing) {
                return false;
            }
        }
    }
    return true;
}

void InjectEncoderWindow(FakeAdapter* adapter, uint8_t missing_finger = 0xFF,
                         uint8_t missing_encoder = 0xFF, bool offline = false,
                         bool malformed = false) {
    for (uint8_t finger = 0; finger < kGloveFingerCount; ++finger) {
        for (uint8_t encoder = 0; encoder < kGloveEncodersPerFinger; ++encoder) {
            if (finger == missing_finger && encoder == missing_encoder && !offline && !malformed) {
                continue;
            }
            auto message = MakeGloveEncoderFrame(
                finger, encoder, static_cast<uint16_t>(finger * 100u + encoder),
                !(offline && finger == missing_finger && encoder == missing_encoder));
            if (malformed && finger == missing_finger && encoder == missing_encoder) {
                message.data.len = 2;
            }
            adapter->InjectMessage(message);
        }
    }
}

bool WaitForRawMessages(const FakeAdapter& adapter, std::size_t expected_count) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (std::chrono::steady_clock::now() < deadline) {
        if (adapter.GetRawSentMessages().size() >= expected_count) {
            return true;
        }
        std::this_thread::yield();
    }
    return adapter.GetRawSentMessages().size() >= expected_count;
}

struct StatusEvents {
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<GloveStatus> snapshots;

    void Push(const GloveStatus& status) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            snapshots.push_back(status);
        }
        condition.notify_all();
    }
};

TEST_F(MotorTestFixture, GloveIsolatesStatusCallbackExceptions) {
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);
    glove->SetOnStatus([](const GloveStatus&) {
        throw std::runtime_error("injected glove status callback failure");
    });

    EXPECT_NO_THROW(InjectEncoderWindow(adapter));
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveStartsWithAnEmptyCurrentCycleStatus) {
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);
    EXPECT_FALSE(glove->GetStatus()[0][0].has_value());
    EXPECT_FALSE(glove->GetStatus()[4][9].has_value());
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GlovePublishesOneCompleteSnapshotWhenAllEncodersUpdate) {
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);

    auto events = std::make_shared<StatusEvents>();
    glove->SetOnStatus([events](const GloveStatus& status) {
        events->Push(status);
    });
    for (int finger = kGloveFingerCount - 1; finger >= 0; --finger) {
        for (int encoder = kGloveEncodersPerFinger - 1; encoder >= 0; --encoder) {
            adapter->InjectMessage(
                MakeGloveEncoderFrame(static_cast<uint8_t>(finger), static_cast<uint8_t>(encoder),
                                      static_cast<uint16_t>(finger * 100 + encoder)));
        }
    }
    {
        std::lock_guard<std::mutex> lock(events->mutex);
        ASSERT_EQ(events->snapshots.size(), 1u);
        ASSERT_TRUE(HasCompleteSnapshot(events->snapshots.front()));
        EXPECT_NEAR(*events->snapshots.front()[4][9], 409.0F / 65535.0F * kTwoPi, 1e-6F);
    }
    const auto current_status = glove->GetStatus();
    EXPECT_TRUE(HasCompleteSnapshot(current_status));
    EXPECT_NEAR(*current_status[0][0], 0.0F, 1e-6F);
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveGetStatusExposesLatestReceivedSnapshot) {
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);

    adapter->InjectMessage(MakeGloveEncoderFrame(3, 4, 12345));

    const auto status = glove->GetStatus();
    ASSERT_TRUE(status[3][4].has_value());
    EXPECT_NEAR(*status[3][4], 12345.0F / 65535.0F * kTwoPi, 1e-6F);
    EXPECT_FALSE(status[0][0].has_value());
    EXPECT_FALSE(status[3][3].has_value());
    EXPECT_FALSE(status[4][9].has_value());

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveGetStatusRetainsLastKnownValuesDuringNextCycle) {
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);

    InjectEncoderWindow(adapter);
    ASSERT_TRUE(HasCompleteSnapshot(glove->GetStatus()));

    adapter->InjectMessage(MakeGloveEncoderFrame(3, 4, 12345));

    const auto status = glove->GetStatus();
    EXPECT_TRUE(HasCompleteSnapshot(status));
    EXPECT_NEAR(*status[3][4], 12345.0F / 65535.0F * kTwoPi, 1e-6F);
    EXPECT_NEAR(*status[1][7], 107.0F / 65535.0F * kTwoPi, 1e-6F);

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveDoesNotPublishAnIncompleteCycle) {
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);

    auto events = std::make_shared<StatusEvents>();
    glove->SetOnStatus([events](const GloveStatus& status) {
        events->Push(status);
    });
    adapter->InjectMessage(MakeGloveEncoderFrame(0, 0, 12345));

    std::unique_lock<std::mutex> lock(events->mutex);
    EXPECT_FALSE(events->condition.wait_for(lock, std::chrono::milliseconds(20), [&] {
        return !events->snapshots.empty();
    }));

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveKeepsMissingEncoderNulloptUntilCycleCompletes) {
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);

    auto events = std::make_shared<StatusEvents>();
    glove->SetOnStatus([events](const GloveStatus& status) {
        events->Push(status);
    });
    InjectEncoderWindow(adapter, 2, 3);
    EXPECT_TRUE(HasOnlyMissingEncoder(glove->GetStatus(), 2, 3));

    adapter->InjectMessage(MakeGloveEncoderFrame(2, 3, 203));
    {
        std::lock_guard<std::mutex> lock(events->mutex);
        ASSERT_EQ(events->snapshots.size(), 1u);
        EXPECT_TRUE(HasCompleteSnapshot(events->snapshots.front()));
    }
    const auto current_status = glove->GetStatus();
    ASSERT_TRUE(current_status[2][3].has_value());
    EXPECT_NEAR(*current_status[2][3], 203.0F / 65535.0F * kTwoPi, 1e-6F);
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveOfflineEncoderCountsAsACycleUpdate) {
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);

    auto events = std::make_shared<StatusEvents>();
    glove->SetOnStatus([events](const GloveStatus& status) {
        events->Push(status);
    });
    InjectEncoderWindow(adapter, 1, 7, true);

    std::lock_guard<std::mutex> lock(events->mutex);
    ASSERT_EQ(events->snapshots.size(), 1u);
    EXPECT_FALSE(events->snapshots.front()[1][7].has_value());
    EXPECT_TRUE(events->snapshots.front()[1][6].has_value());
    const auto current_status = glove->GetStatus();
    EXPECT_FALSE(current_status[1][7].has_value());
    EXPECT_TRUE(current_status[1][6].has_value());
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveMalformedEncoderFrameDoesNotCompleteCycle) {
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);

    auto events = std::make_shared<StatusEvents>();
    glove->SetOnStatus([events](const GloveStatus& status) {
        events->Push(status);
    });

    InjectEncoderWindow(adapter);
    {
        std::lock_guard<std::mutex> lock(events->mutex);
        ASSERT_EQ(events->snapshots.size(), 1u);
    }
    InjectEncoderWindow(adapter, 3, 4, false, true);
    const auto current_status = glove->GetStatus();
    ASSERT_TRUE(HasCompleteSnapshot(current_status));
    ASSERT_TRUE(current_status[3][4].has_value());
    EXPECT_NEAR(*current_status[3][4], 304.0F / 65535.0F * kTwoPi, 1e-6F);

    std::unique_lock<std::mutex> lock(events->mutex);
    EXPECT_FALSE(events->condition.wait_for(lock, std::chrono::milliseconds(20), [&] {
        return events->snapshots.size() > 1u;
    }));
    lock.unlock();

    adapter->InjectMessage(MakeGloveEncoderFrame(3, 4, 304));
    std::lock_guard<std::mutex> event_lock(events->mutex);
    ASSERT_EQ(events->snapshots.size(), 2u);
    EXPECT_TRUE(HasCompleteSnapshot(events->snapshots.back()));
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveIgnoresEncoderDataReceivedBeforeFacadePublication) {
    auto& manager = EncosDriverManager::Instance();
    std::mutex mutex;
    std::condition_variable condition;
    bool creation_paused = false;
    bool release_creation = false;
    DriverManagerTestAccess::SetCreationHook(manager, [&](EncosDriverManager::CreationStage stage) {
        if (stage != EncosDriverManager::CreationStage::BeforeGloveFacadePublish) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex);
        creation_paused = true;
        condition.notify_all();
        condition.wait(lock, [&] {
            return release_creation;
        });
    });
    auto creation = std::async(std::launch::async, [this] {
        return adapter->GetGlove(0);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, std::chrono::milliseconds(100), [&] {
            return creation_paused;
        }));
    }
    adapter->InjectMessage(MakeGloveEncoderFrame(0, 0, 40000));
    auto* reserved_bus = adapter->GetBus(0, 1);
    ASSERT_NE(reserved_bus, nullptr);
    EXPECT_FALSE(manager.DestroyBus(reserved_bus));
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_creation = true;
    }
    condition.notify_all();
    auto* glove = creation.get();
    DriverManagerTestAccess::SetCreationHook(manager, {});

    ASSERT_NE(glove, nullptr);
    EXPECT_FALSE(glove->GetStatus()[0][0].has_value());
    EXPECT_TRUE(manager.DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveDoesNotCollectFramesBeforeItsReceiveRoutesActivate) {
    auto& manager = EncosDriverManager::Instance();
    std::mutex mutex;
    std::condition_variable condition;
    bool callbacks_connected = false;
    bool release_creation = false;
    DriverManagerTestAccess::SetCreationHook(manager, [&](EncosDriverManager::CreationStage stage) {
        if (stage != EncosDriverManager::CreationStage::AfterGloveCallbacksConnected) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex);
        callbacks_connected = true;
        condition.notify_all();
        condition.wait(lock, [&] {
            return release_creation;
        });
    });
    auto creation = std::async(std::launch::async, [this] {
        return adapter->GetGlove(0);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, std::chrono::milliseconds(100), [&] {
            return callbacks_connected;
        }));
    }

    adapter->InjectMessage(MakeGloveEncoderFrame(0, 0, 40000));

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_creation = true;
    }
    condition.notify_all();
    auto* glove = creation.get();
    DriverManagerTestAccess::SetCreationHook(manager, {});

    ASSERT_NE(glove, nullptr);
    EXPECT_FALSE(glove->GetStatus()[0][0].has_value());
    EXPECT_TRUE(manager.DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveCreationRollbackAfterCallbackConnectionReleasesAllDevices) {
    auto& manager = EncosDriverManager::Instance();
    DriverManagerTestAccess::SetCreationHook(manager, [](EncosDriverManager::CreationStage stage) {
        if (stage == EncosDriverManager::CreationStage::AfterGloveCallbacksConnected) {
            throw std::runtime_error("injected glove callback connection failure");
        }
    });
    EXPECT_THROW(adapter->GetGlove(0), std::runtime_error);

    DriverManagerTestAccess::SetCreationHook(manager, {});
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);
    EXPECT_TRUE(manager.DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveCalibrationByMaskWaitsForAValidResponse) {
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);
    adapter->ClearCommandRecords();

    adapter->SetDecodedCommandObserver([this](const FakeCommandRecord& record) {
        if (record.bus_idx == 2) {
            adapter->InjectMessage(MakeGloveCalibrationResponse(2, 0xA2));
        }
    });
    EXPECT_EQ(
        glove->CalibrateByMask(static_cast<uint8_t>(GloveFinger::Middle), ENCOS_GLOVE_CALI_E(7)),
        GloveCalibrationStatus::Limited);
    adapter->ClearDecodedCommandObserver();

    const auto messages = adapter->GetRawSentMessages();
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages.front().bus_idx, 2);
    EXPECT_EQ(messages.front().data.data[0], 0xA1);
    EXPECT_EQ(messages.front().data.data[1], 0x80);
    EXPECT_EQ(messages.front().data.data[2], 0x00);

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveCalibrationTimeoutIgnoresInvalidResponses) {
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);
    adapter->ClearCommandRecords();

    const auto begin = std::chrono::steady_clock::now();
    auto calibration = std::async(std::launch::async, [glove] {
        return glove->CalibrateByMask(1, ENCOS_GLOVE_CALI_E(0));
    });
    ASSERT_TRUE(WaitForRawMessages(*adapter, 1));
    auto malformed = MakeGloveCalibrationResponse(1, 0xA0);
    malformed.data.len = 7;
    adapter->InjectMessage(malformed);
    adapter->InjectMessage(MakeGloveCalibrationResponse(1, 0x00));
    adapter->InjectMessage(MakeGloveCalibrationResponse(1, 0x7F));

    EXPECT_EQ(calibration.get(), GloveCalibrationStatus::Timeout);
    EXPECT_GE(std::chrono::steady_clock::now() - begin, std::chrono::milliseconds(3));
    EXPECT_THROW(glove->CalibrateByMask(5, ENCOS_GLOVE_CALI_E(0)), std::invalid_argument);
    EXPECT_THROW(glove->CalibrateByMask(0, 0x0400u), std::invalid_argument);
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveCalibrationThrowsWhenGloveIsRetiring) {
    auto& manager = EncosDriverManager::Instance();
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);

    bool calibration_attempted = false;
    DriverManagerTestAccess::SetDeletionHook(
        manager, [glove, &calibration_attempted](EncosDriverManager::DeletionStage stage) {
            if (stage != EncosDriverManager::DeletionStage::BeforeDeviceDestroy ||
                calibration_attempted) {
                return;
            }
            calibration_attempted = true;
            EXPECT_THROW(glove->CalibrateAll(), std::runtime_error);
            EXPECT_THROW(glove->CalibrateByMask(0, ENCOS_GLOVE_CALI_E(0)), std::runtime_error);
        });

    EXPECT_TRUE(manager.DestroyGlove(glove));
    DriverManagerTestAccess::SetDeletionHook(manager, {});
    EXPECT_TRUE(calibration_attempted);
}

TEST_F(MotorTestFixture, GloveCalibrateAllStopsAfterFirstFingerTimeout) {
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);
    adapter->ClearCommandRecords();

    std::mutex mutex;
    std::condition_variable condition;
    bool second_command_sent = false;
    adapter->SetDecodedCommandObserver([&](const FakeCommandRecord& record) {
        if (record.bus_idx == 1) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                second_command_sent = true;
            }
            condition.notify_all();
        }
    });

    auto calibration = std::async(std::launch::async, [glove] {
        return glove->CalibrateAll();
    });
    ASSERT_TRUE(WaitForRawMessages(*adapter, 1));
    {
        std::unique_lock<std::mutex> lock(mutex);
        EXPECT_FALSE(condition.wait_for(lock, std::chrono::milliseconds(15), [&] {
            return second_command_sent;
        }));
    }
    EXPECT_EQ(calibration.get(), GloveCalibrationStatus::Timeout);
    adapter->ClearDecodedCommandObserver();

    const auto messages = adapter->GetRawSentMessages();
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages.front().bus_idx, 0);
    EXPECT_EQ(messages.front().data.id, 0x8150u);
    EXPECT_EQ(messages.front().data.len, 8);
    EXPECT_EQ(messages.front().data.data[0], 0xA0);
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveCalibrateAllWaitsForEachResponseAndMergesResults) {
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);
    adapter->ClearCommandRecords();

    constexpr std::array<uint8_t, kGloveFingerCount> responses = {0xA0, 0xA2, 0xA1, 0xA2, 0xA0};
    adapter->SetDecodedCommandObserver([this, responses](const FakeCommandRecord& record) {
        if (record.bus_idx < 0 || record.bus_idx >= static_cast<int>(responses.size())) {
            return;
        }
        const auto finger_idx = static_cast<uint8_t>(record.bus_idx);
        adapter->InjectMessage(MakeGloveCalibrationResponse(finger_idx, responses[finger_idx]));
    });

    const auto calibration = glove->CalibrateAll();
    adapter->ClearDecodedCommandObserver();

    EXPECT_EQ(calibration, GloveCalibrationStatus::Failed);
    const auto messages = adapter->GetRawSentMessages();
    ASSERT_EQ(messages.size(), kGloveFingerCount);
    for (uint8_t finger = 0; finger < kGloveFingerCount; ++finger) {
        EXPECT_EQ(messages[finger].bus_idx, finger);
        EXPECT_EQ(messages[finger].data.id, 0x8150u);
        EXPECT_EQ(messages[finger].data.data[0], 0xA0);
    }
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveQueuesCalibrationRequestsWithoutInterleavingCommands) {
    auto& manager = EncosDriverManager::Instance();
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);
    adapter->ClearCommandRecords();

    std::mutex command_mutex;
    std::condition_variable command_condition;
    bool first_all_command_observed = false;
    bool allow_all_calibration_to_continue = false;
    adapter->SetDecodedCommandObserver(
        [this, &command_mutex, &command_condition, &first_all_command_observed,
         &allow_all_calibration_to_continue](const FakeCommandRecord& record) {
            if (record.bus_idx < 0 || record.bus_idx >= kGloveFingerCount) {
                return;
            }
            if (record.bus_idx == 0) {
                std::unique_lock<std::mutex> lock(command_mutex);
                first_all_command_observed = true;
                command_condition.notify_all();
                command_condition.wait(lock, [&allow_all_calibration_to_continue] {
                    return allow_all_calibration_to_continue;
                });
            }
            adapter->InjectMessage(MakeGloveCalibrationResponse(
                static_cast<uint8_t>(record.bus_idx),
                static_cast<uint8_t>(GloveCalibrationStatus::Success)));
        });
    auto all_calibration = std::async(std::launch::async, [glove] {
        return glove->CalibrateAll();
    });
    {
        std::unique_lock<std::mutex> lock(command_mutex);
        const bool first_all_command_entered = command_condition.wait_for(
            lock, std::chrono::milliseconds(100), [&first_all_command_observed] {
                return first_all_command_observed;
            });
        if (!first_all_command_entered) {
            lock.unlock();
            {
                std::lock_guard<std::mutex> command_lock(command_mutex);
                allow_all_calibration_to_continue = true;
            }
            command_condition.notify_all();
            EXPECT_EQ(all_calibration.get(), GloveCalibrationStatus::Success);
            adapter->ClearDecodedCommandObserver();
            FAIL() << "First whole-hand calibration command did not enter the observer";
        }
    }
    auto masked_calibration = std::async(std::launch::async, [glove] {
        return glove->CalibrateByMask(1, ENCOS_GLOVE_CALI_E(1));
    });
    EXPECT_EQ(masked_calibration.wait_for(std::chrono::milliseconds(12)),
              std::future_status::timeout);
    {
        std::lock_guard<std::mutex> lock(command_mutex);
        allow_all_calibration_to_continue = true;
    }
    command_condition.notify_all();

    EXPECT_EQ(all_calibration.get(), GloveCalibrationStatus::Success);
    EXPECT_EQ(masked_calibration.get(), GloveCalibrationStatus::Success);
    adapter->ClearDecodedCommandObserver();

    const auto messages = adapter->GetRawSentMessages();
    ASSERT_EQ(messages.size(), 6u);
    for (uint8_t finger = 0; finger < kGloveFingerCount; ++finger) {
        EXPECT_EQ(messages[finger].bus_idx, finger);
        EXPECT_EQ(messages[finger].data.data[0], 0xA0);
    }
    EXPECT_EQ(messages.back().bus_idx, 1);
    EXPECT_EQ(messages.back().data.data[0], 0xA1);
    EXPECT_TRUE(manager.DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveDestructionWaitsForInFlightCalibration) {
    auto& manager = EncosDriverManager::Instance();
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);
    adapter->ClearCommandRecords();

    std::mutex command_mutex;
    std::condition_variable command_condition;
    bool first_all_command_observed = false;
    bool allow_all_calibration_to_continue = false;
    adapter->SetDecodedCommandObserver(
        [this, &command_mutex, &command_condition, &first_all_command_observed,
         &allow_all_calibration_to_continue](const FakeCommandRecord& record) {
            if (record.bus_idx < 0 || record.bus_idx >= kGloveFingerCount) {
                return;
            }
            if (record.bus_idx == 0) {
                std::unique_lock<std::mutex> lock(command_mutex);
                first_all_command_observed = true;
                command_condition.notify_all();
                command_condition.wait(lock, [&allow_all_calibration_to_continue] {
                    return allow_all_calibration_to_continue;
                });
            }
            adapter->InjectMessage(MakeGloveCalibrationResponse(
                static_cast<uint8_t>(record.bus_idx),
                static_cast<uint8_t>(GloveCalibrationStatus::Success)));
        });

    auto calibration = std::async(std::launch::async, [glove] {
        return glove->CalibrateAll();
    });
    {
        std::unique_lock<std::mutex> lock(command_mutex);
        ASSERT_TRUE(command_condition.wait_for(lock, std::chrono::milliseconds(100), [&] {
            return first_all_command_observed;
        }));
    }
    auto destruction = std::async(std::launch::async, [&manager, glove] {
        return manager.DestroyGlove(glove);
    });
    EXPECT_EQ(destruction.wait_for(std::chrono::milliseconds(5)), std::future_status::timeout);

    {
        std::lock_guard<std::mutex> lock(command_mutex);
        allow_all_calibration_to_continue = true;
    }
    command_condition.notify_all();

    EXPECT_EQ(calibration.get(), GloveCalibrationStatus::Success);
    EXPECT_TRUE(destruction.get());
    adapter->ClearDecodedCommandObserver();
}

TEST_F(MotorTestFixture, GloveDestructionDrainsQueuedCalibrationCalls) {
    auto& manager = EncosDriverManager::Instance();
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);
    adapter->ClearCommandRecords();

    auto first = std::async(std::launch::async, [glove] {
        return glove->CalibrateByMask(0, ENCOS_GLOVE_CALI_E(0));
    });
    ASSERT_TRUE(WaitForRawMessages(*adapter, 1));
    auto second = std::async(std::launch::async, [glove] {
        return glove->CalibrateByMask(1, ENCOS_GLOVE_CALI_E(1));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_TRUE(manager.DestroyGlove(glove));
    EXPECT_EQ(first.get(), GloveCalibrationStatus::Timeout);
    EXPECT_EQ(second.get(), GloveCalibrationStatus::Timeout);
}

TEST_F(MotorTestFixture, GloveProtectsInternalBusesAndRejectsDestructionInStatusCallback) {
    auto& manager = EncosDriverManager::Instance();
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);
    auto* internal_bus = adapter->GetBus(0, 1);
    ASSERT_NE(internal_bus, nullptr);
    EXPECT_FALSE(manager.DestroyBus(internal_bus));
    EXPECT_FALSE(DeleteBus(internal_bus));

    std::atomic<bool> attempted{false};
    std::atomic<bool> result{true};
    glove->SetOnStatus([&](const GloveStatus&) {
        result.store(manager.DestroyGlove(glove));
        attempted.store(true);
    });
    InjectEncoderWindow(adapter);
    EXPECT_TRUE(attempted.load());
    EXPECT_FALSE(result.load());
    EXPECT_TRUE(manager.DestroyGlove(glove));
}

TEST_F(MotorTestFixture, GloveStatusCallbackRejectsAdapterDestruction) {
    auto& manager = EncosDriverManager::Instance();
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);

    std::atomic<bool> attempted{false};
    std::atomic<bool> result{true};
    glove->SetOnStatus([&](const GloveStatus&) {
        result.store(manager.DestroyAdapter(adapter));
        attempted.store(true);
    });
    InjectEncoderWindow(adapter);
    EXPECT_TRUE(attempted.load());
    EXPECT_FALSE(result.load());
    EXPECT_TRUE(manager.DestroyAdapter(adapter));

    static int replacement_count = 0;
    const std::string replacement_name =
        "GloveCallbackReplacementAdapter" + std::to_string(replacement_count++);
    adapter = static_cast<FakeAdapter*>(
        manager.CreateAdapterWithFactory(replacement_name, [replacement_name]() {
            return new FakeAdapter(replacement_name);
        }));
    bus = nullptr;
    motor = nullptr;
}

TEST_F(MotorTestFixture, DestroyAdapterCascadesGloveAndAllowsFreshCreation) {
    auto& manager = EncosDriverManager::Instance();
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);
    ASSERT_TRUE(manager.DestroyAdapter(adapter));

    static int recreate_count = 0;
    const std::string recreate_name = "GloveRecreateAdapter" + std::to_string(recreate_count++);
    adapter = static_cast<FakeAdapter*>(
        manager.CreateAdapterWithFactory(recreate_name, [recreate_name]() {
            return new FakeAdapter(recreate_name);
        }));
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    bus = adapter->GetBus(0);
    motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    auto* recreated = adapter->GetGlove(0);
    ASSERT_NE(recreated, nullptr);
    EXPECT_FALSE(recreated->GetStatus()[0][0].has_value());
    EXPECT_TRUE(manager.DestroyGlove(recreated));
}

TEST_F(MotorTestFixture, DestroyAdapterWaitsForInFlightGloveCalibration) {
    auto& manager = EncosDriverManager::Instance();
    auto* glove = adapter->GetGlove(0);
    ASSERT_NE(glove, nullptr);

    std::mutex mutex;
    std::condition_variable condition;
    bool first_command_observed = false;
    bool allow_calibration_to_continue = false;
    adapter->SetDecodedCommandObserver(
        [this, &mutex, &condition, &first_command_observed,
         &allow_calibration_to_continue](const FakeCommandRecord& record) {
            if (record.bus_idx < 0 || record.bus_idx >= kGloveFingerCount) {
                return;
            }
            if (record.bus_idx == 0) {
                std::unique_lock<std::mutex> lock(mutex);
                first_command_observed = true;
                condition.notify_all();
                condition.wait(lock, [&allow_calibration_to_continue] {
                    return allow_calibration_to_continue;
                });
            }
            adapter->InjectMessage(MakeGloveCalibrationResponse(
                static_cast<uint8_t>(record.bus_idx),
                static_cast<uint8_t>(GloveCalibrationStatus::Success)));
        });

    auto calibration = std::async(std::launch::async, [glove] {
        return glove->CalibrateAll();
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, std::chrono::milliseconds(100), [&] {
            return first_command_observed;
        }));
    }
    auto destruction = std::async(std::launch::async, [this, &manager] {
        return manager.DestroyAdapter(adapter);
    });
    EXPECT_EQ(destruction.wait_for(std::chrono::milliseconds(5)), std::future_status::timeout);

    {
        std::lock_guard<std::mutex> lock(mutex);
        allow_calibration_to_continue = true;
    }
    condition.notify_all();

    EXPECT_EQ(calibration.get(), GloveCalibrationStatus::Success);
    EXPECT_TRUE(destruction.get());

    static int replacement_count = 0;
    const std::string replacement_name =
        "GloveCalibrationReplacementAdapter" + std::to_string(replacement_count++);
    adapter = static_cast<FakeAdapter*>(
        manager.CreateAdapterWithFactory(replacement_name, [replacement_name] {
            return new FakeAdapter(replacement_name);
        }));
    bus = nullptr;
    motor = nullptr;
}

}  // namespace
}  // namespace encos
