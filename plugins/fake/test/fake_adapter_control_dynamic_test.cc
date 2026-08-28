#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>

#include "adapter/base_adapter.h"
#include "adapter/fake_adapter_control.h"
#include "encos_motor.h"
#include "motor/motor.h"
#include "plugins/fake/fake_adapter.h"

namespace encos {

namespace fs = std::filesystem;

class FakeAdapterControlDynamicTest : public ::testing::Test {
protected:
    void SetUp() override {
        setenv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").c_str(), 1);
    }
};

TEST_F(FakeAdapterControlDynamicTest, QueryReturnsControlForFakePlugin) {
    auto adapter = MakeAdapter("Fake", "fake-control-test");
    ASSERT_NE(adapter, nullptr);

    auto control = adapter->GetFakeAdapterControl();
    ASSERT_NE(control, nullptr);
}

TEST_F(FakeAdapterControlDynamicTest, QueryReturnsNullForNonFakeAdapter) {
    class StubAdapter : public BaseAdapter {
    public:
        StubAdapter() : BaseAdapter("stub", "StubAdapter") {}

        std::unordered_map<int, Bus*> GetBuses() override {
            return {};
        }
        bool Ok() override {
            return true;
        }

    protected:
        void Send(const MotorMessage&) override {}
    };

    auto adapter = std::make_shared<StubAdapter>();

    auto control = adapter->GetFakeAdapterControl();
    EXPECT_EQ(control, nullptr);
}

TEST_F(FakeAdapterControlDynamicTest, ObserverReceivesDecodedCommand) {
    auto adapter = MakeAdapter("Fake", "fake-observer-test");
    auto control = adapter->GetFakeAdapterControl();
    ASSERT_NE(control, nullptr);

    control->EnableAutoCreateMotor();
    control->SetReplyMode(FakeReplyMode::Automatic);

    FakeCommandRecord observed{};
    bool called = false;
    control->SetDecodedCommandObserver([&observed, &called](const FakeCommandRecord& record) {
        observed = record;
        called = true;
    });

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(5, MotorModel::EC_A4310_P2);
    motor->SpdControl<0>(1.5f, 2.5f);
    adapter->Commit();

    EXPECT_TRUE(called);
    EXPECT_EQ(observed.kind, FakeCommandKind::SpdControl);
    EXPECT_EQ(observed.bus_idx, 0);
    EXPECT_EQ(observed.motor_idx, 5);

    const auto* payload = std::get_if<FakeSpdControlPayload>(&observed.payload);
    ASSERT_NE(payload, nullptr);
    EXPECT_NEAR(payload->speed, 1.5f, kDecodedAngleTolerance);
    EXPECT_NEAR(payload->current, 2.5f, kDecodedFloatTolerance);
}

TEST_F(FakeAdapterControlDynamicTest, AutomaticReplyProducesFeedback) {
    auto adapter = MakeAdapter("Fake", "fake-reply-test");
    auto control = adapter->GetFakeAdapterControl();
    ASSERT_NE(control, nullptr);

    control->EnableAutoCreateMotor();
    control->SetReplyMode(FakeReplyMode::Automatic);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(6, MotorModel::EC_A4310_P2);
    motor->PosControl<0>(0.75f, 1.0f, 2.0f, 1);
    adapter->Commit();

    const auto status = motor->GetStatus();
    ASSERT_TRUE(status.has_value());
    EXPECT_NEAR(status->position, 0.75f, kDecodedAngleTolerance);
}

TEST_F(FakeAdapterControlDynamicTest, ManualReplySuppressesFeedback) {
    auto adapter = MakeAdapter("Fake", "fake-manual-test");
    auto control = adapter->GetFakeAdapterControl();
    ASSERT_NE(control, nullptr);

    control->EnableAutoCreateMotor();
    control->SetReplyMode(FakeReplyMode::Manual);

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(7, MotorModel::EC_A4310_P2);
    motor->PosControl<0>(0.5f, 1.0f, 2.0f, 1);
    adapter->Commit();

    const auto status = motor->GetStatus();
    EXPECT_FALSE(status.has_value());
}

TEST_F(FakeAdapterControlDynamicTest, DisabledRecordingKeepsObserver) {
    auto adapter = MakeAdapter("Fake", "fake-recording-test");
    auto control = adapter->GetFakeAdapterControl();
    ASSERT_NE(control, nullptr);

    control->EnableAutoCreateMotor();
    control->SetReplyMode(FakeReplyMode::Automatic);
    control->EnableCommandRecording(false);

    int observer_count = 0;
    control->SetDecodedCommandObserver([&observer_count](const FakeCommandRecord&) {
        ++observer_count;
    });

    auto bus = adapter->GetBus(0);
    auto motor = bus->GetMotor(8, MotorModel::EC_A4310_P2);
    motor->CurControl<0>(1.0f, 1);
    adapter->Commit();

    EXPECT_EQ(observer_count, 1);
}

}  // namespace encos
