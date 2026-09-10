#include <algorithm>
#include <array>
#include <future>
#include <gtest/gtest.h>
#include <memory>

#include "ethercat_handle.h"

namespace {
struct SimulatedLink {
    uint16 state = EC_STATE_OPERATIONAL;
    uint16 requested = EC_STATE_NONE;
    int wkc = 3;
    unsigned exchanges = 0;
    unsigned reconfigurations = 0;
    unsigned recoveries = 0;
    unsigned callbacks = 0;
    unsigned stop_after = 0;
    EthercatHandle* owner = nullptr;
    std::array<uint8_t, sizeof(EthercatCanFdMsg8)> output{};
    std::array<uint8_t, sizeof(EthercatCanFdMsg8)> last_output{};
};
SimulatedLink* simulated_link = nullptr;
}  // namespace

class EthercatHandleTestAccess {
public:
    static std::unique_ptr<EthercatHandle> Create() {
        auto handle = std::unique_ptr<EthercatHandle>(new EthercatHandle(
            encos::CreateLogger("ethercat_recovery_test", encos::LogLevel::Off)));
        handle->running_.store(true);
        handle->in_operational_.store(true);
        handle->ctx_.slavecount = 1;
        handle->slave_configs_ = {EthercatHandle::ClassifyOutputPdoSize(sizeof(EthercatCanFdMsg8))};
        return handle;
    }
    static void Attach(EthercatHandle& handle, SimulatedLink& simulation) {
        simulated_link = &simulation;
        simulation.owner = &handle;
        handle.expected_wkc_.store(3);
        handle.slaves_operational_ = true;
        handle.ctx_.slavelist[1].outputs = simulation.output.data();
        handle.ctx_.slavelist[1].Obytes = simulation.output.size();
        handle.io_.read_state = [](ecx_contextt* context) {
            context->slavelist[1].state = simulated_link->state;
            return static_cast<int>(simulated_link->state);
        };
        handle.io_.write_state = [](ecx_contextt* context, uint16 slave) {
            simulated_link->requested = context->slavelist[slave].state;
            return 1;
        };
        handle.io_.state_check = [](ecx_contextt* context, uint16 slave, uint16, int) {
            context->slavelist[slave].state = simulated_link->state;
            return simulated_link->state;
        };
        handle.io_.reconfigure = [](ecx_contextt*, uint16, int) {
            ++simulated_link->reconfigurations;
            simulated_link->state = EC_STATE_SAFE_OP;
            return static_cast<int>(EC_STATE_SAFE_OP);
        };
        handle.io_.recover = [](ecx_contextt*, uint16, int) {
            ++simulated_link->recoveries;
            return 0;
        };
        handle.io_.send = [](ecx_contextt*) {
            ++simulated_link->exchanges;
            simulated_link->last_output = simulated_link->output;
            return 1;
        };
        handle.io_.receive = [](ecx_contextt*, int) {
            if (simulated_link->stop_after != 0 &&
                simulated_link->exchanges >= simulated_link->stop_after) {
                simulated_link->owner->RequestStop();
            }
            return simulated_link->wkc;
        };
        handle.SetReceiveCallback([](const encos::MotorMessages&) {
            ++simulated_link->callbacks;
        });
    }
    static void Check(EthercatHandle& handle) {
        handle.CheckState();
    }
    static void Exchange(EthercatHandle& handle) {
        handle.ExchangeOnce();
    }
    static void AddSecondSlave(EthercatHandle& handle, uint16 state) {
        handle.ctx_.slavecount = 2;
        handle.ctx_.slavelist[2].state = state;
    }
    static void FailStateReadWithCachedOp(EthercatHandle& handle) {
        handle.ctx_.slavelist[1].state = EC_STATE_OPERATIONAL;
        handle.io_.read_state = [](ecx_contextt*) {
            return 0;
        };
    }
    static void Degrade(EthercatHandle& handle) {
        handle.DegradedHandler();
    }
    static bool Running(EthercatHandle& handle) {
        return handle.running_.load();
    }
    static bool Operational(EthercatHandle& handle) {
        return handle.in_operational_.load();
    }
    static bool TakeFrame(EthercatHandle& handle) {
        EthercatHandle::OutputFrame frame;
        return handle.PrepareNextFrame(frame, 1);
    }
};

TEST(EthercatRecoveryTests, DegradationKeepsRecoveryAliveAndAcceptsNormalCommands) {
    auto handle = EthercatHandleTestAccess::Create();
    encos::MotorMessage command{};
    command.data.id = 1;
    command.data.len = 1;
    command.data.data[0] = 0xE0;
    handle->Send(command);
    EthercatHandleTestAccess::Degrade(*handle);
    EXPECT_TRUE(EthercatHandleTestAccess::Running(*handle));
    EXPECT_FALSE(EthercatHandleTestAccess::Operational(*handle));
    EXPECT_TRUE(EthercatHandleTestAccess::TakeFrame(*handle));
    handle->Send(command);
    EXPECT_TRUE(EthercatHandleTestAccess::TakeFrame(*handle));
}

TEST(EthercatRecoveryTests, LongOutageKeepsNormalPdoExchangeAndRecoversAutomatically) {
    SimulatedLink simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::Attach(*handle, simulation);
    encos::MotorMessage command{};
    command.data.id = 1;
    command.data.len = 1;
    command.data.data[0] = 0xE0;
    for (int i = 0; i < 20; ++i) {
        handle->Send(command);
    }
    simulation.wkc = 0;
    simulation.state = EC_STATE_NONE;
    EthercatHandleTestAccess::Exchange(*handle);
    ASSERT_FALSE(handle->Ok());
    for (int i = 0; i < 150; ++i) {
        handle->Send(command);
        EthercatHandleTestAccess::Check(*handle);
        EthercatHandleTestAccess::Exchange(*handle);
        EXPECT_TRUE(std::any_of(simulation.last_output.begin(), simulation.last_output.end(),
                                [](uint8_t byte) {
                                    return byte != 0;
                                }));
    }
    EXPECT_TRUE(EthercatHandleTestAccess::Running(*handle));
    EXPECT_EQ(simulation.exchanges, 151U);
    EXPECT_EQ(simulation.callbacks, 0U);
    EXPECT_GT(simulation.recoveries, 0U);

    simulation.state = EC_STATE_SAFE_OP;
    simulation.wkc = 3;
    EthercatHandleTestAccess::Check(*handle);
    EXPECT_EQ(simulation.requested, EC_STATE_OPERATIONAL);
    EthercatHandleTestAccess::Exchange(*handle);
    EXPECT_FALSE(handle->Ok());
    simulation.state = EC_STATE_OPERATIONAL;
    EthercatHandleTestAccess::Check(*handle);
    EthercatHandleTestAccess::Exchange(*handle);
    ASSERT_TRUE(handle->Ok());
    EXPECT_FALSE(EthercatHandleTestAccess::TakeFrame(*handle));
    EXPECT_EQ(simulation.callbacks, 1U);
    handle->Send(command);
    EthercatHandleTestAccess::Exchange(*handle);
    EXPECT_TRUE(
        std::any_of(simulation.last_output.begin(), simulation.last_output.end(), [](uint8_t byte) {
            return byte != 0;
        }));
    EXPECT_EQ(simulation.callbacks, 2U);
}

TEST(EthercatRecoveryTests, SafeOpErrorAndPreOpRequireConfirmedStateAndFreshWkc) {
    SimulatedLink simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::Attach(*handle, simulation);
    simulation.state = EC_STATE_SAFE_OP + EC_STATE_ERROR;
    EthercatHandleTestAccess::Check(*handle);
    EXPECT_FALSE(handle->Ok());
    EXPECT_EQ(simulation.requested, EC_STATE_SAFE_OP + EC_STATE_ACK);
    simulation.state = EC_STATE_PRE_OP;
    EthercatHandleTestAccess::Check(*handle);
    EXPECT_EQ(simulation.reconfigurations, 1U);
    EthercatHandleTestAccess::Check(*handle);
    EXPECT_EQ(simulation.requested, EC_STATE_OPERATIONAL);
    simulation.state = EC_STATE_OPERATIONAL;
    EthercatHandleTestAccess::Check(*handle);
    simulation.wkc = 0;
    EthercatHandleTestAccess::Exchange(*handle);
    EXPECT_FALSE(handle->Ok());
    simulation.wkc = 3;
    EthercatHandleTestAccess::Exchange(*handle);
    EXPECT_FALSE(handle->Ok());
    EthercatHandleTestAccess::Check(*handle);
    EthercatHandleTestAccess::Exchange(*handle);
    EXPECT_TRUE(handle->Ok());
    handle->RequestStop();
    EthercatHandleTestAccess::Check(*handle);
    EthercatHandleTestAccess::Exchange(*handle);
    EXPECT_FALSE(handle->Ok());
    EXPECT_FALSE(EthercatHandleTestAccess::Running(*handle));
}

TEST(EthercatRecoveryTests, FailedStateReadAndOtherNonOpSlaveCannotResumeTraffic) {
    SimulatedLink simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::Attach(*handle, simulation);
    EthercatHandleTestAccess::AddSecondSlave(*handle, EC_STATE_SAFE_OP);
    EthercatHandleTestAccess::Check(*handle);
    EthercatHandleTestAccess::Exchange(*handle);
    EXPECT_FALSE(handle->Ok());
    EthercatHandleTestAccess::FailStateReadWithCachedOp(*handle);
    EthercatHandleTestAccess::Check(*handle);
    EthercatHandleTestAccess::Exchange(*handle);
    EXPECT_FALSE(handle->Ok());
}

TEST(EthercatRecoveryTests, ActualLoopContinuesPastOldErrorLimitAndStopsOnRequest) {
    SimulatedLink simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::Attach(*handle, simulation);
    simulation.state = EC_STATE_NONE;
    simulation.wkc = 0;
    simulation.stop_after = 60;
    auto loop = std::async(std::launch::async, [&] {
        handle->Loop(std::chrono::microseconds(100));
    });
    const auto result = loop.wait_for(std::chrono::seconds(2));
    EXPECT_EQ(result, std::future_status::ready);
    handle->RequestStop();
    loop.get();
    EXPECT_EQ(simulation.exchanges, 60U);
    EXPECT_FALSE(handle->Ok());
    EXPECT_EQ(simulation.callbacks, 0U);
}

TEST(EthercatRecoveryTests, RecoveryUsesExistingBacklogPruningInsteadOfSpecialClearing) {
    SimulatedLink simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::Attach(*handle, simulation);
    simulation.wkc = 0;
    EthercatHandleTestAccess::Degrade(*handle);
    for (unsigned id = 1; id <= 4; ++id) {
        encos::MotorMessage command{};
        command.data.id = id;
        command.data.len = 1;
        handle->SendSynchronized({command});
    }
    EthercatHandleTestAccess::Exchange(*handle);
    EthercatCanFdMsg8 packet{};
    std::memcpy(&packet, simulation.last_output.data(), sizeof(packet));
    EXPECT_EQ(packet.motor[0].id, 4U);
    EXPECT_FALSE(EthercatHandleTestAccess::TakeFrame(*handle));
}
