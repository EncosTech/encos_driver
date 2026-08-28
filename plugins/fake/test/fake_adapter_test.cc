#include "plugins/fake/fake_adapter.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <thread>
#include <variant>

#include "bus/bus.h"
#include "managed_adapter_test.h"
#include "motor/motor.h"

namespace encos {

namespace {

std::vector<FakeCommandRecord> WaitForCommands(const FakeAdapter& adapter, std::size_t count) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    auto commands = adapter.GetCommandRecords();
    while (commands.size() < count && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        commands = adapter.GetCommandRecords();
    }
    EXPECT_GE(commands.size(), count);
    return commands;
}

std::optional<MotorStatus> WaitForStatus(Motor& motor) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    auto status = motor.GetStatus(0);
    while (!status.has_value() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        status = motor.GetStatus(0);
    }
    return status;
}

}  // namespace

// 确保 FakeCommandRecord 不再携带 received_time，从而无法使用时间窗口裁剪。
template <typename T, typename = void>
struct HasReceivedTime : std::false_type {};
template <typename T>
struct HasReceivedTime<T, std::void_t<decltype(std::declval<T>().received_time)>> : std::true_type {
};
static_assert(!HasReceivedTime<FakeCommandRecord>::value,
              "FakeCommandRecord should not carry a received_time field");

TEST(FakeAdapterTests, SeedMotorFromModelCreatesSnapshotAndDecodesSpdControlCommand) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    ASSERT_NE(motor, nullptr);
    motor->SpdControl<0>(1.25f, 2.5f);

    const auto commands = WaitForCommands(*adapter, 1);
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::SpdControl);
    EXPECT_EQ(commands[0].bus_idx, 0);
    EXPECT_EQ(commands[0].motor_idx, 1);

    const auto* payload = std::get_if<FakeSpdControlPayload>(&commands[0].payload);
    ASSERT_NE(payload, nullptr);
    EXPECT_NEAR(payload->speed, 1.25f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload->current, 2.5f, kDecodedFloatTolerance);
    EXPECT_EQ(payload->feedback_type, 0);

    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot.speed_rad_s, payload->speed, kDecodedAngleTolerance);
    EXPECT_NEAR(snapshot.current_a, payload->current, kDecodedFloatTolerance);
}

TEST(FakeAdapterTests, NegativeBusIndexStatusUsesStableUniqueKey) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-negative-bus");
    constexpr int kBusIndex = -1;
    constexpr int kMotorIndex = 1;
    adapter->SeedMotor(kBusIndex, kMotorIndex, MotorModel::EC_A4310_P2);
    ASSERT_NE(adapter->GetBus(kBusIndex)->GetMotor(kMotorIndex, MotorModel::EC_A4310_P2), nullptr);

    MotorStatus expected{};
    expected.error = MotorError::NoError;
    expected.position = 0.25f;
    expected.speed = 1.5f;
    expected.current = 2.0f;
    expected.motor_temperature = 25.0f;
    expected.mos_temperature = 30.0f;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(kBusIndex, kMotorIndex, expected));

    const auto actual = adapter->GetMotorStatus(kBusIndex, kMotorIndex, 0);
    ASSERT_TRUE(actual.has_value());
    EXPECT_NEAR(actual->position, expected.position, kDecodedAngleTolerance);
    EXPECT_NEAR(actual->speed, expected.speed, kDecodedAngleTolerance);
    EXPECT_NEAR(actual->current, expected.current, kDecodedFloatTolerance);
}

TEST(FakeAdapterTests, AutomaticWritePolicyCanIgnoreParameterAck) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    adapter->SetParameterWritePolicy(0, 1, MotorParameter::CanTimeout, FakeWritePolicy::Ignore);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    EXPECT_FALSE(motor->SetCanTimeout(1000, true));

    const auto& commands = adapter->GetCommandRecords();
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::SetParameter);

    const auto* payload = std::get_if<FakeSetParameterPayload>(&commands[0].payload);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->parameter, MotorParameter::CanTimeout);
}

TEST(FakeAdapterTests, GetBusesIncludesCreatedAndSeededBuses) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    auto held_bus = adapter->GetBus(3);
    adapter->SeedMotor(-1, 3, MotorModel::EC_A4310_P2);
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    adapter->SeedMotor(5, 2, MotorModel::EC_A4310_P2);

    const auto buses = adapter->GetBuses();
    EXPECT_NE(buses.find(-1), buses.end());
    EXPECT_NE(buses.find(0), buses.end());
    EXPECT_NE(buses.find(3), buses.end());
    EXPECT_NE(buses.find(5), buses.end());
    EXPECT_EQ(buses.at(3), held_bus);
}

TEST(FakeAdapterTests, InjectionRoutesRegisteredUnknownAndUnknownBusFrames) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-injection-routing");
    auto* bus = adapter->GetBus(2);
    auto* motor = bus->GetMotor(7, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);

    int callback_count = 0;
    motor->SetOnStatus([&](const MotorStatus&) {
        ++callback_count;
    });
    MotorStatus status{};
    status.error = MotorError::NoError;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(2, 7, status));
    EXPECT_EQ(callback_count, 1);

    std::thread inject_unknown([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        MotorMessage message{};
        message.bus_idx = 2;
        message.data.id = 0x654;
        message.data.len = 1;
        adapter->InjectMessage(message);
    });
    EXPECT_TRUE(bus->DetectExternalDevice());
    inject_unknown.join();

    MotorMessage unknown_bus{};
    unknown_bus.bus_idx = 99;
    unknown_bus.data.id = 0x655;
    unknown_bus.data.len = 1;
    adapter->InjectMessage(unknown_bus);
    EXPECT_EQ(EncosDriverManager::Instance().GetBuses(adapter.get()).count(99), 0u);
}

TEST(FakeAdapterTests, ManagerOwnsControlAndDeletesTheCompleteFakeSubtree) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-manager-lifecycle");
    auto* adapter_pointer = adapter.get();
    auto* control = adapter->GetFakeAdapterControl();
    ASSERT_EQ(control, static_cast<FakeAdapterControl*>(adapter_pointer));

    adapter->SeedMotor(4, 8, MotorModel::EC_A4310_P2);
    auto* bus = adapter->GetBuses().at(4);
    auto* motor = bus->GetMotor(8, MotorModel::EC_A4310_P2);
    ASSERT_NE(motor, nullptr);

    auto& manager = EncosDriverManager::Instance();
    EXPECT_TRUE(manager.DestroyAdapter(adapter_pointer));
    EXPECT_FALSE(manager.DestroyMotor(motor));
    EXPECT_FALSE(manager.DestroyBus(bus));
    EXPECT_FALSE(manager.DestroyAdapter(adapter_pointer));
    EXPECT_FALSE(manager.DestroyAdapterByInterfaceName("fake-manager-lifecycle"));
}

TEST(FakeAdapterTests, PVTDecodeUsesSeededMotorRanges) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->SeedMotor(0, 1, MotorModel::EC_A10010_P2);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A10010_P2);

    const auto ranges = adapter->GetMotorSnapshot(0, 1).ranges;
    const float torque = ranges.torque.max * 0.75f;
    motor->PVTControl<0>(ranges.kp.max, ranges.kd.max, ranges.position.max, ranges.speed.max,
                         torque);

    const auto commands = WaitForCommands(*adapter, 1);
    ASSERT_FALSE(commands.empty());
    const auto* payload = std::get_if<FakePVTControlPayload>(&commands.back().payload);
    ASSERT_NE(payload, nullptr);
    EXPECT_NEAR(payload->kp, ranges.kp.max, kDecodedFloatTolerance);
    EXPECT_NEAR(payload->kd, ranges.kd.max, kDecodedFloatTolerance);
    EXPECT_NEAR(payload->position, ranges.position.max, kDecodedAngleTolerance);
    EXPECT_NEAR(payload->speed, ranges.speed.max, kDecodedAngleTolerance);
    EXPECT_NEAR(payload->torque, torque, 0.2f);
}

TEST(FakeAdapterTests, AutoCreateMotorIsDisabledByDefault) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(7, MotorModel::EC_A4310_P2);

    motor->PosControl<0>(1.0f, 2.0f, 3.0f, 1);

    const auto commands = WaitForCommands(*adapter, 1);
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::PosControl);

    EXPECT_THROW(adapter->GetMotorSnapshot(0, 7), std::out_of_range);
}

TEST(FakeAdapterTests, EnableAutoCreateMotorCreatesSnapshotAndReplies) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->EnableAutoCreateMotor();

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(7, MotorModel::EC_A4310_P2);

    motor->PosControl<0>(1.25f, 2.0f, 3.0f, 1);

    ASSERT_EQ(WaitForCommands(*adapter, 1).size(), 1U);
    const auto snapshot = adapter->GetMotorSnapshot(0, 7);
    EXPECT_EQ(snapshot.model, MotorModel::EC_A4310_P2);
    EXPECT_NEAR(snapshot.position_rad, 1.25f, kDecodedAngleTolerance);

    const auto status = WaitForStatus(*motor);
    ASSERT_TRUE(status.has_value());
    EXPECT_NEAR(status->position, 1.25f, kDecodedAngleTolerance);
}

TEST(FakeAdapterTests, PosControlDoesNotIntegrateSpeedLimitAfterReachingTarget) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->EnableAutoCreateMotor();
    adapter->EnablePositionError(false);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(7, MotorModel::EC_A4310_P2);

    const auto feedback = motor->PosControl<3>(1.25f, 2.0f, 3.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_NEAR(feedback.speed, 2.0f, 0.02f);
    EXPECT_NEAR(feedback.current, 3.0f, kDecodedFloatTolerance);

    const auto snapshot = adapter->GetMotorSnapshot(0, 7);
    EXPECT_NEAR(snapshot.position_rad, 1.25f, kDecodedAngleTolerance);
    EXPECT_NEAR(snapshot.speed_rad_s, 0.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(snapshot.current_a, 3.0f, kDecodedFloatTolerance);
}

TEST(FakeAdapterTests, AutoCreateMotorInitializesPositionToZeroBeforeSpeedControl) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->EnableAutoCreateMotor();

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(9, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(2.0f, 1.0f, 1);
    adapter->Commit();

    const auto snapshot = adapter->GetMotorSnapshot(0, 9);
    EXPECT_NEAR(snapshot.position_rad, 0.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(snapshot.speed_rad_s, 2.0f, kDecodedAngleTolerance);

    const auto status = motor->GetStatus();
    ASSERT_TRUE(status.has_value());
    EXPECT_NEAR(status->position, 0.0f, kDecodedAngleTolerance);
}

TEST(FakeAdapterTests, EnableAutoCreateMotorWithManualReplyDoesNotEmitFeedback) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->EnableAutoCreateMotor();
    adapter->SetReplyMode(FakeReplyMode::Manual);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(8, MotorModel::EC_A4310_P2);

    motor->PosControl<0>(0.5f, 1.0f, 2.0f, 1);
    adapter->Commit();

    EXPECT_NO_THROW(adapter->GetMotorSnapshot(0, 8));

    const auto status = motor->GetStatus();
    EXPECT_FALSE(status.has_value());
}

TEST(FakeAdapterTests, ParameterReadFirstAutoCreatesAndUsesGeneratedRanges) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->EnableAutoCreateMotor();

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(3, MotorModel::EC_A4310_P2);

    const auto kt = motor->GetParameter<MotorParameter::Kt>();
    const auto ranges = motor->GetPVTRanges();

    const auto snapshot = adapter->GetMotorSnapshot(0, 3);
    EXPECT_EQ(snapshot.model, MotorModel::EC_A4310_P2);
    EXPECT_NEAR(snapshot.kt, kt, kDecodedFloatTolerance);
    EXPECT_NEAR(snapshot.ranges.position.min, ranges.position.min, kDecodedAngleTolerance);
    EXPECT_NEAR(snapshot.ranges.position.max, ranges.position.max, kDecodedAngleTolerance);

    const auto& commands = adapter->GetCommandRecords();
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::GetParameter);
}

TEST(FakeAdapterTests, AutoCreateUsesEC_A4310_P2RangesForFirstCommandDecoding) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->EnableAutoCreateMotor();

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(4, MotorModel::EC_A4310_P2);

    // 使用 EC_A4310_P2 的最大 PVT 范围发送命令
    const auto ranges = GetMotorModelRanges(MotorModel::EC_A4310_P2);
    motor->PVTControl<0>(ranges.kp.max, ranges.kd.max, ranges.position.max, ranges.speed.max,
                         ranges.torque.max);

    ASSERT_EQ(WaitForCommands(*adapter, 1).size(), 1U);
    const auto snapshot = adapter->GetMotorSnapshot(0, 4);
    EXPECT_EQ(snapshot.model, MotorModel::EC_A4310_P2);

    const auto commands = WaitForCommands(*adapter, 1);
    ASSERT_FALSE(commands.empty());
    const auto* payload = std::get_if<FakePVTControlPayload>(&commands.back().payload);
    ASSERT_NE(payload, nullptr);
    EXPECT_NEAR(payload->position, ranges.position.max, kDecodedAngleTolerance);
    EXPECT_NEAR(payload->speed, ranges.speed.max, kDecodedAngleTolerance);
    EXPECT_NEAR(payload->torque, ranges.torque.max, 0.2f);
}

TEST(FakeAdapterTests, DecodedCommandObserverReceivesEachRecordOnce) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->EnableAutoCreateMotor();

    std::vector<FakeCommandRecord> observed;
    adapter->SetDecodedCommandObserver([&observed](const FakeCommandRecord& record) {
        observed.push_back(record);
    });

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(5, MotorModel::EC_A4310_P2);
    motor->SpdControl<0>(1.5f, 2.5f);
    adapter->Commit();

    ASSERT_EQ(observed.size(), 1);
    EXPECT_EQ(observed[0].kind, FakeCommandKind::SpdControl);
    EXPECT_EQ(observed[0].bus_idx, 0);
    EXPECT_EQ(observed[0].motor_idx, 5);

    const auto* payload = std::get_if<FakeSpdControlPayload>(&observed[0].payload);
    ASSERT_NE(payload, nullptr);
    EXPECT_NEAR(payload->speed, 1.5f, kDecodedAngleTolerance);
}

TEST(FakeAdapterTests, ClearDecodedCommandObserverStopsNotifications) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->EnableAutoCreateMotor();

    std::vector<FakeCommandRecord> observed;
    adapter->SetDecodedCommandObserver([&observed](const FakeCommandRecord& record) {
        observed.push_back(record);
    });
    adapter->ClearDecodedCommandObserver();

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(6, MotorModel::EC_A4310_P2);
    motor->PosControl<0>(1.0f, 2.0f, 3.0f, 1);

    EXPECT_TRUE(observed.empty());

    const auto commands = WaitForCommands(*adapter, 1);
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::PosControl);
}

TEST(FakeAdapterTests, ObserverDoesNotChangeNormalRecordingAndReply) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->EnableAutoCreateMotor();

    std::vector<FakeCommandRecord> observed;
    adapter->SetDecodedCommandObserver([&observed](const FakeCommandRecord& record) {
        observed.push_back(record);
    });

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(7, MotorModel::EC_A4310_P2);
    motor->PosControl<0>(1.25f, 2.0f, 3.0f, 1);
    adapter->Commit();

    const auto snapshot = adapter->GetMotorSnapshot(0, 7);
    EXPECT_NEAR(snapshot.position_rad, 1.25f, kDecodedAngleTolerance);

    const auto status = motor->GetStatus();
    ASSERT_TRUE(status.has_value());
    EXPECT_NEAR(status->position, 1.25f, kDecodedAngleTolerance);

    ASSERT_EQ(observed.size(), 1);
    EXPECT_EQ(observed[0].kind, FakeCommandKind::PosControl);
}

TEST(FakeAdapterTests, ConcurrentCommandsHistoryReadsAndObserverReentryAreSafe) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-concurrent-state");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
    adapter->SeedMotor(0, 2, MotorModel::EC_A4310_P2);

    auto* bus = adapter->GetBus(0);
    auto* first_motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);
    auto* second_motor = bus->GetMotor(2, MotorModel::EC_A4310_P2);
    ASSERT_NE(first_motor, nullptr);
    ASSERT_NE(second_motor, nullptr);

    constexpr int kIterations = 200;
    std::atomic<int> observed_count{0};
    std::atomic<int> ready_count{0};
    std::atomic<int> writers_remaining{2};
    std::atomic<bool> start{false};

    adapter->SetDecodedCommandObserver([&](const FakeCommandRecord& record) {
        (void) adapter->GetCommandRecords();
        (void) adapter->GetMotorSnapshot(record.bus_idx, record.motor_idx);
        observed_count.fetch_add(1, std::memory_order_relaxed);
    });

    auto writer = [&](Motor* motor, int motor_idx) {
        ready_count.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            if (iteration % 2 == 0) {
                EXPECT_EQ(motor->SpdControl<1>(static_cast<float>(motor_idx), 1.0f).error,
                          MotorError::NoError);
            } else {
                EXPECT_TRUE(motor->SetCanTimeout(static_cast<uint16_t>(1000 + iteration), true));
            }
        }
        writers_remaining.fetch_sub(1, std::memory_order_release);
    };

    std::thread first_writer(writer, first_motor, 1);
    std::thread second_writer(writer, second_motor, 2);
    std::thread reader([&]() {
        ready_count.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        do {
            (void) adapter->GetCommandRecords();
            (void) adapter->GetFormattedSentCommands();
            (void) adapter->GetReplyRecords();
            (void) adapter->GetRawSentMessages();
            (void) adapter->GetMotorSnapshot(0, 1);
            (void) adapter->GetMotorSnapshot(0, 2);
        } while (writers_remaining.load(std::memory_order_acquire) != 0);
    });

    while (ready_count.load(std::memory_order_acquire) != 3) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    first_writer.join();
    second_writer.join();
    reader.join();

    EXPECT_EQ(WaitForCommands(*adapter, 2U * kIterations).size(), 2U * kIterations);
    EXPECT_EQ(adapter->GetRawSentMessages().size(), 2U * kIterations);
    EXPECT_EQ(adapter->GetReplyRecords().size(), kIterations);
    EXPECT_EQ(observed_count.load(std::memory_order_relaxed), 2 * kIterations);
}

TEST(FakeAdapterTests, CommandRecordsAreNotPrunedByTimeWindow) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(1.0f, 2.0f);
    // 如果存在 10 秒时间窗口裁剪，等待超过 10 秒后第一条记录会被删除。
    std::this_thread::sleep_for(std::chrono::milliseconds(10500));
    motor->SpdControl<0>(2.0f, 3.0f);

    const auto commands = WaitForCommands(*adapter, 2);
    EXPECT_EQ(commands.size(), 2u);
}

TEST(FakeAdapterTests, GetParameterReadsSimulatedRunningState) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    FakeMotorSnapshot snapshot;
    snapshot.position_rad = 1.0f;
    snapshot.speed_rad_s = 0.0f;
    snapshot.current_a = 3.0f;
    snapshot.acceleration = 1000.0f;
    adapter->SeedMotor(0, 1, snapshot);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    EXPECT_NEAR(motor->GetParameter<MotorParameter::Position>(), 1.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(motor->GetParameter<MotorParameter::Current>(), 3.0f, kDecodedFloatTolerance);

    motor->SpdControl<0>(2.0f, 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_NEAR(motor->GetParameter<MotorParameter::Speed>(), 2.0f, kDecodedAngleTolerance);
}

TEST(FakeAdapterTests, SpdControlAdvancesPositionWithinErrorBound) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    auto seed_snapshot = adapter->CreateSnapshot(MotorModel::EC_A4310_P2);
    seed_snapshot.acceleration = 1000.0f;
    adapter->SeedMotor(0, 1, seed_snapshot);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(2.0f, 1.0f);
    ASSERT_EQ(WaitForCommands(*adapter, 1).size(), 1U);
    const auto t1 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    motor->SpdControl<0>(1.0f, 0.5f);
    ASSERT_EQ(WaitForCommands(*adapter, 2).size(), 2U);
    const auto t2 = std::chrono::steady_clock::now();

    const float elapsed = std::chrono::duration<float>(t2 - t1).count();
    // 速度控制直接跳到命令速度，等待期间按上一速度 2.0 rad/s 匀速推进。
    const float expected_increment = 2.0f * elapsed;

    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot.speed_rad_s, 1.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(snapshot.current_a, 0.5f, kDecodedFloatTolerance);
    // 允许 10% 随机误差和少量调度/测量开销。
    EXPECT_GE(snapshot.position_rad, expected_increment * 0.85f - kDecodedAngleTolerance);
    EXPECT_LE(snapshot.position_rad, expected_increment * 1.15f + kDecodedAngleTolerance);
}

TEST(FakeAdapterTests, DisabledPositionErrorUsesDeterministicIncrement) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->EnablePositionError(false);
    auto seed_snapshot = adapter->CreateSnapshot(MotorModel::EC_A4310_P2);
    seed_snapshot.acceleration = 1000.0f;
    adapter->SeedMotor(0, 1, seed_snapshot);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(2.0f, 1.0f);
    ASSERT_EQ(WaitForCommands(*adapter, 1).size(), 1U);
    const auto t1 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    motor->SpdControl<0>(1.0f, 0.5f);
    ASSERT_EQ(WaitForCommands(*adapter, 2).size(), 2U);
    const auto t2 = std::chrono::steady_clock::now();

    const float elapsed = std::chrono::duration<float>(t2 - t1).count();
    const float expected_increment = 2.0f * elapsed;

    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot.speed_rad_s, 1.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(snapshot.current_a, 0.5f, kDecodedFloatTolerance);
    EXPECT_NEAR(snapshot.position_rad, expected_increment, 0.02f);
}

TEST(FakeAdapterTests, CurControlContinuesPriorSimulatedMotion) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    auto seed_snapshot = adapter->CreateSnapshot(MotorModel::EC_A4310_P2);
    seed_snapshot.acceleration = 1000.0f;
    adapter->SeedMotor(0, 1, seed_snapshot);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(2.0f, 1.0f);
    const auto t1 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    motor->CurControl<0>(0.5f, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto t2 = std::chrono::steady_clock::now();

    const float elapsed = std::chrono::duration<float>(t2 - t1).count();
    // 速度控制直接跳到 2.0 rad/s，电流控制按电流比例产生加速度，速度会增大。
    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot.current_a, 0.5f, kDecodedFloatTolerance);
    EXPECT_GT(snapshot.speed_rad_s, 2.0f + kDecodedAngleTolerance);
    EXPECT_GT(snapshot.position_rad, 2.0f * elapsed * 0.85f - kDecodedAngleTolerance);
}

TEST(FakeAdapterTests, TorControlAdvancesPositionWithinErrorBound) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    auto seed_snapshot = adapter->CreateSnapshot(MotorModel::EC_A4310_P2);
    seed_snapshot.acceleration = 1000.0f;
    adapter->SeedMotor(0, 1, seed_snapshot);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(2.0f, 1.0f);
    const auto t1 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    motor->TorControl<0>(0.3f, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto t2 = std::chrono::steady_clock::now();

    const float elapsed = std::chrono::duration<float>(t2 - t1).count();
    // 速度控制直接跳到 2.0 rad/s，扭矩控制按扭矩比例产生加速度，速度会增大。
    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot.torque_nm, 0.3f, kDecodedFloatTolerance);
    EXPECT_NEAR(snapshot.current_a, 0.3f, kDecodedFloatTolerance);
    EXPECT_GT(snapshot.speed_rad_s, 2.0f + kDecodedAngleTolerance);
    EXPECT_GT(snapshot.position_rad, 2.0f * elapsed * 0.85f - kDecodedAngleTolerance);
}

TEST(FakeAdapterTests, RunningParameterReadAdvancesPositionAfterSpeedCommand) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    auto seed_snapshot = adapter->CreateSnapshot(MotorModel::EC_A4310_P2);
    seed_snapshot.acceleration = 1000.0f;
    adapter->SeedMotor(0, 1, seed_snapshot);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(2.0f, 1.0f);
    const auto t1 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const float position = motor->GetParameter<MotorParameter::Position>();
    const auto t2 = std::chrono::steady_clock::now();

    const float elapsed = std::chrono::duration<float>(t2 - t1).count();
    // 速度控制直接跳到 2.0 rad/s，参数读取前按该速度匀速推进。
    const float expected_increment = 2.0f * elapsed;

    EXPECT_GE(position, expected_increment * 0.85f - kDecodedAngleTolerance);
    EXPECT_LE(position, expected_increment * 1.15f + kDecodedAngleTolerance);
}

TEST(FakeAdapterTests, SpdControlSetsSpeedDirectly) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    auto seed_snapshot = adapter->CreateSnapshot(MotorModel::EC_A4310_P2);
    seed_snapshot.acceleration = 10.0f;
    adapter->SeedMotor(0, 1, seed_snapshot);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(2.0f, 1.0f);
    ASSERT_EQ(WaitForCommands(*adapter, 1).size(), 1U);
    const auto snapshot_after_first = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot_after_first.speed_rad_s, 2.0f, kDecodedAngleTolerance);

    const auto t1 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    motor->SpdControl<0>(2.0f, 0.5f);
    ASSERT_EQ(WaitForCommands(*adapter, 2).size(), 2U);
    const auto t2 = std::chrono::steady_clock::now();

    const float elapsed = std::chrono::duration<float>(t2 - t1).count();
    // 速度直接跳到 2.0 rad/s，第二次命令前按该速度匀速推进。
    const float expected_increment = 2.0f * elapsed;

    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot.speed_rad_s, 2.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(snapshot.current_a, 0.5f, kDecodedFloatTolerance);
    EXPECT_GE(snapshot.position_rad, expected_increment * 0.85f - kDecodedAngleTolerance);
    EXPECT_LE(snapshot.position_rad, expected_increment * 1.15f + kDecodedAngleTolerance);
}

TEST(FakeAdapterTests, CurControlAcceleratesBasedOnCurrent) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    auto seed_snapshot = adapter->CreateSnapshot(MotorModel::EC_A4310_P2);
    seed_snapshot.acceleration = 10.0f;
    adapter->SeedMotor(0, 1, seed_snapshot);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(2.0f, 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    motor->CurControl<0>(15.0f, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot.current_a, 15.0f, kDecodedFloatTolerance);
    // 正电流产生正方向加速度，速度应大于之前的 2.0 rad/s
    EXPECT_GT(snapshot.speed_rad_s, 2.0f + kDecodedAngleTolerance);
}

TEST(FakeAdapterTests, CurControlNegativeCurrentDecelerates) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    auto seed_snapshot = adapter->CreateSnapshot(MotorModel::EC_A4310_P2);
    seed_snapshot.acceleration = 10.0f;
    adapter->SeedMotor(0, 1, seed_snapshot);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(2.0f, 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    motor->CurControl<0>(-15.0f, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot.current_a, -15.0f, kDecodedFloatTolerance);
    // 负电流产生反方向加速度，速度应小于之前的 2.0 rad/s
    EXPECT_LT(snapshot.speed_rad_s, 2.0f - kDecodedAngleTolerance);
}

TEST(FakeAdapterTests, ManualReplyModeRecordsCommandsWithoutChangingSnapshots) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-test");
    adapter->EnableAutoCreateMotor();
    adapter->SetReplyMode(FakeReplyMode::Manual);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);

    motor->SpdControl<0>(2.0f, 1.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    motor->CurControl<0>(0.5f, 1);
    adapter->Commit();

    const auto snapshot = adapter->GetMotorSnapshot(0, 1);
    EXPECT_NEAR(snapshot.position_rad, 0.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(snapshot.speed_rad_s, 0.0f, kDecodedAngleTolerance);
    EXPECT_NEAR(snapshot.current_a, 0.0f, kDecodedFloatTolerance);

    const auto commands = WaitForCommands(*adapter, 2);
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[0].kind, FakeCommandKind::SpdControl);
    EXPECT_EQ(commands[1].kind, FakeCommandKind::CurControl);
}

}  // namespace encos
