#include "adapter/fake_adapter_control.h"

#include <cmath>
#include <gtest/gtest.h>

#include "adapter/base_adapter.h"
#include "bus/bus.h"
#include "managed_adapter_test.h"
#include "motor/motor.h"
#include "plugins/fake/fake_adapter.h"

namespace encos {

namespace {

class StubAdapter : public BaseAdapter {
public:
    explicit StubAdapter(const std::string& interface_name)
        : BaseAdapter(interface_name, "StubAdapter") {}

    std::unordered_map<int, Bus*> GetBuses() override {
        return {};
    }
    bool Ok() override {
        return true;
    }

protected:
    void Send(const MotorMessage&) override {}
};

}  // namespace

TEST(FakeAdapterControlStaticTest, QueryReturnsNullForNonFakeAdapter) {
    auto adapter = MakeManagedAdapter<StubAdapter>("fake-control-stub");

    auto control = adapter->GetFakeAdapterControl();
    EXPECT_EQ(control, nullptr);
}

TEST(FakeAdapterControlStaticTest, ObserverReceivesDecodedCommand) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-observer-test");
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

TEST(FakeAdapterControlStaticTest, DisabledRecordingKeepsObserverAndStopsHistory) {
    auto adapter = MakeManagedAdapter<FakeAdapter>("fake-recording-test");
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
    EXPECT_TRUE(adapter->GetCommandRecords().empty());

    control->EnableCommandRecording(true);
    motor->CurControl<0>(2.0f, 1);
    adapter->Commit();
    EXPECT_EQ(adapter->GetCommandRecords().size(), 1u);
}

}  // namespace encos
