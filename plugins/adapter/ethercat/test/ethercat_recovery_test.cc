// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <future>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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
struct LifecycleSimulation {
    std::map<std::pair<uint16, uint16>, std::vector<uint8>> registers;
    std::vector<std::string> events;
    unsigned operations = 0;
    unsigned fail_operation = 0;
    bool discard_writes = false;
    bool disconnected = false;
    bool stall_state_check = false;
    int state_timeout = 0;
    uint16 requested = EC_STATE_NONE;
    void Set(uint16 slave, uint16 address, uint16 value) {
        registers[{slave, address}] = {static_cast<uint8>(value), static_cast<uint8>(value >> 8)};
    }
    uint16 Get(uint16 slave, uint16 address) {
        const auto& bytes = registers.at({slave, address});
        return bytes[0] | (bytes.size() > 1 ? static_cast<uint16>(bytes[1] << 8) : 0);
    }
};
LifecycleSimulation* lifecycle = nullptr;
std::vector<std::pair<uint16, uint16>> timing_writes;
}  // namespace

class EthercatHandleTestAccess {
public:
    static void AttachLifecycle(EthercatHandle& handle, LifecycleSimulation& simulation,
                                bool operational = true) {
        lifecycle = &simulation;
        handle.context_closed_.store(false);
        handle.operational_requested_ = operational;
        handle.ctx_.slavecount = 2;
        for (int slave = 1; slave <= 2; ++slave) {
            auto& info = handle.ctx_.slavelist[slave];
            info.configadr = static_cast<uint16>(slave);
            info.Obytes = 1120;
            info.SMtype[2] = 3;
            info.SM[2].StartAddr = htoes(0x1100);
            info.SM[2].SMlength = htoes(1120);
            info.SM[2].SMflags = htoel(0x10024);
            simulation.Set(slave, 0x0400, 2498);
            simulation.Set(slave, 0x0420, 0);
            simulation.registers[{slave, 0x0814}] = {0x24};
            simulation.registers[{slave, 0x0816}] = {1};
        }
        handle.io_.read_register = [](ecx_portt*, uint16 slave, uint16 address, uint16 length,
                                      void* data, int) {
            if (++lifecycle->operations == lifecycle->fail_operation) {
                return 0;
            }
            const auto& bytes = lifecycle->registers.at({slave, address});
            EXPECT_EQ(length, bytes.size());
            std::memcpy(data, bytes.data(), length);
            return 1;
        };
        handle.io_.write_register = [](ecx_portt*, uint16 slave, uint16 address, uint16 length,
                                       void* data, int) {
            if (++lifecycle->operations == lifecycle->fail_operation) {
                return 0;
            }
            if (address == 0x0814) {
                EXPECT_EQ(lifecycle->Get(slave, 0x0816) & 1, 0);
            }
            if (!lifecycle->discard_writes) {
                const auto* bytes = static_cast<const uint8*>(data);
                lifecycle->registers[{slave, address}] = {bytes, bytes + length};
            }
            return 1;
        };
        handle.io_.write_state = [](ecx_contextt* context, uint16 slave) {
            EXPECT_EQ(slave, 0);
            lifecycle->requested = context->slavelist[slave].state;
            lifecycle->events.emplace_back("request");
            return lifecycle->disconnected ? 0 : context->slavecount;
        };
        handle.io_.state_check = [](ecx_contextt* context, uint16 slave, uint16 state,
                                    int timeout) {
            lifecycle->state_timeout = timeout;
            if (lifecycle->stall_state_check) {
                std::this_thread::sleep_for(std::chrono::microseconds(timeout));
            }
            lifecycle->events.push_back("check" + std::to_string(slave));
            context->slavelist[slave].state = lifecycle->disconnected ? EC_STATE_NONE : state;
            return context->slavelist[slave].state;
        };
        handle.io_.close = [](ecx_contextt*) {
            lifecycle->events.emplace_back("close");
        };
    }
    static bool ConfigureWatchdogs(EthercatHandle& handle) {
        return handle.ConfigureWatchdogs();
    }
    static int RestoreWatchdog(EthercatHandle& handle, uint16 slave) {
        return handle.ctx_.slavelist[slave].PO2SOconfig(&handle.ctx_, slave);
    }
    static void SetInputOnly(EthercatHandle& handle, int slave) {
        handle.ctx_.slavelist[slave].Obytes = 0;
        handle.ctx_.slavelist[slave].Obits = 0;
    }
    static void RemoveOutputSm(EthercatHandle& handle) {
        handle.ctx_.slavelist[1].SMtype[2] = 4;
    }
    static uint32 SmFlags(EthercatHandle& handle) {
        return etohl(handle.ctx_.slavelist[1].SM[2].SMflags);
    }
    static AdapterExtend ParseOptions(EthercatHandle& handle, const std::string& json) {
        handle.extend_ = AdapterExtend::Parse(json);
        handle.ParseWatchdogOptions();
        return handle.extend_;
    }
    static void ConfigureTimingSlaves(EthercatHandle& handle,
                                      const std::vector<std::size_t>& counts,
                                      const std::string& json) {
        handle.ctx_.slavecount = static_cast<int>(counts.size());
        handle.slave_configs_.resize(counts.size());
        for (std::size_t slave = 0; slave < counts.size(); ++slave) {
            handle.slave_configs_[slave].bus_count = counts[slave];
        }
        handle.extend_ = AdapterExtend::Parse(json);
        timing_writes.clear();
        handle.io_.sdo_write = [](ecx_contextt*, uint16 slave, uint16 index, uint8, boolean, int,
                                  const void*, int) {
            timing_writes.emplace_back(slave, index);
            return 1;
        };
    }
    static bool ApplyTimings(EthercatHandle& handle) {
        return handle.ApplyChannelTimings();
    }
    static bool WaitForRequests(EthercatHandle& handle, std::size_t count) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock(handle.reconfigure_mutex_);
                if (handle.pending_reconfigure_.size() == count) {
                    return true;
                }
            }
            std::this_thread::yield();
        }
        handle.RequestStop();
        return false;
    }
    static void ServiceRequest(EthercatHandle& handle) {
        handle.ServicePendingReconfigure();
    }
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
    static int WkcErrorCount(EthercatHandle& handle) {
        return handle.wkc_error_count_.load();
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
    EthercatHandleTestAccess::Check(*handle);
    EthercatHandleTestAccess::Exchange(*handle);
    ASSERT_FALSE(handle->Ok());
    for (int i = 0; i < 150; ++i) {
        handle->Send(command);
        EthercatHandleTestAccess::Check(*handle);
        EthercatHandleTestAccess::Exchange(*handle);
        EXPECT_FALSE(std::any_of(simulation.last_output.begin(), simulation.last_output.end(),
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
    EXPECT_TRUE(EthercatHandleTestAccess::TakeFrame(*handle));
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

TEST(EthercatRecoveryTests, StartupChecksSlaveStateBeforeCountingBadWkc) {
    SimulatedLink simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::Attach(*handle, simulation);
    encos::MotorMessage command{};
    command.data.id = 1;
    command.data.len = 1;
    command.data.data[0] = 0xE0;
    handle->Send(command);
    simulation.state = EC_STATE_SAFE_OP + EC_STATE_ERROR;
    simulation.wkc = 0;
    simulation.stop_after = 60;

    auto loop = std::async(std::launch::async, [&] {
        handle->Loop(std::chrono::microseconds(100));
    });
    ASSERT_EQ(loop.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    loop.get();

    EXPECT_EQ(simulation.requested, EC_STATE_SAFE_OP + EC_STATE_ACK);
    EXPECT_EQ(EthercatHandleTestAccess::WkcErrorCount(*handle), 0);
    EXPECT_TRUE(EthercatHandleTestAccess::TakeFrame(*handle));
    EXPECT_FALSE(handle->Ok());
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
    EXPECT_TRUE(EthercatHandleTestAccess::TakeFrame(*handle));
    EXPECT_FALSE(EthercatHandleTestAccess::TakeFrame(*handle));
}

TEST(EthercatRecoveryTests, AccumulatesWkcLossesAcrossSuccessfulExchanges) {
    SimulatedLink simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::Attach(*handle, simulation);
    for (int i = 0; i < 19; ++i) {
        simulation.wkc = 0;
        EthercatHandleTestAccess::Exchange(*handle);
        EXPECT_TRUE(handle->Ok());
        simulation.wkc = 3;
        EthercatHandleTestAccess::Exchange(*handle);
        EXPECT_TRUE(handle->Ok());
    }
    EXPECT_EQ(simulation.callbacks, 19U);
    simulation.wkc = 0;
    EthercatHandleTestAccess::Exchange(*handle);
    EXPECT_FALSE(handle->Ok());
    EXPECT_TRUE(EthercatHandleTestAccess::Running(*handle));
    simulation.wkc = 3;
    EthercatHandleTestAccess::Check(*handle);
    EthercatHandleTestAccess::Exchange(*handle);
    EXPECT_TRUE(handle->Ok());
    EXPECT_EQ(simulation.callbacks, 20U);
}
TEST(EthercatRecoveryTests, WkcWindowExpiresAfter100Exchanges) {
    SimulatedLink simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::Attach(*handle, simulation);
    for (int window = 0; window < 3; ++window) {
        for (int i = 0; i < 100; ++i) {
            simulation.wkc = i < 19 ? 0 : 3;
            EthercatHandleTestAccess::Exchange(*handle);
            EXPECT_TRUE(handle->Ok());
        }
    }
}

TEST(EthercatRecoveryTests, ConcurrentReconfigurationsKeepSeparateResults) {
    auto handle = EthercatHandleTestAccess::Create();
    auto first = std::async(std::launch::async, [&] {
        return handle->ReconfigureBuses(AdapterExtend::Parse(R"({"buses":{"0:0":{"bitrate":0}}})"));
    });
    ASSERT_TRUE(EthercatHandleTestAccess::WaitForRequests(*handle, 1));
    auto second = std::async(std::launch::async, [&] {
        return handle->ReconfigureBuses(AdapterExtend{});
    });
    ASSERT_TRUE(EthercatHandleTestAccess::WaitForRequests(*handle, 2));
    EthercatHandleTestAccess::ServiceRequest(*handle);
    EXPECT_THROW(first.get(), std::exception);
    EXPECT_EQ(second.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
    EthercatHandleTestAccess::ServiceRequest(*handle);
    EXPECT_TRUE(second.get());
}

TEST(EthercatRecoveryTests, StoppingFailsEveryQueuedReconfiguration) {
    auto handle = EthercatHandleTestAccess::Create();
    auto first = std::async(std::launch::async, [&] {
        return handle->ReconfigureBuses(AdapterExtend{});
    });
    auto second = std::async(std::launch::async, [&] {
        return handle->ReconfigureBuses(AdapterExtend{});
    });
    ASSERT_TRUE(EthercatHandleTestAccess::WaitForRequests(*handle, 2));
    handle->RequestStop();
    EXPECT_THROW(first.get(), std::runtime_error);
    EXPECT_THROW(second.get(), std::runtime_error);
}

TEST(EthercatLifecycleTests, StopRequestsSafeOpAndConfirmsEachSlaveBeforeClosingOnce) {
    LifecycleSimulation simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::AttachLifecycle(*handle, simulation);
    handle->RequestStop();
    EXPECT_TRUE(simulation.events.empty());
    handle->Stop();
    EXPECT_EQ(simulation.requested, EC_STATE_SAFE_OP);
    EXPECT_EQ(simulation.events,
              (std::vector<std::string>{"request", "check1", "check2", "close"}));
    EXPECT_GT(simulation.state_timeout, 0);
    EXPECT_LE(simulation.state_timeout, 500000);
    EXPECT_FALSE(handle->Ok());
    handle->Stop();
    handle.reset();
    EXPECT_EQ(simulation.events.size(), 4U);
}

TEST(EthercatLifecycleTests, DisconnectedShutdownStillClosesWithoutSendingPdos) {
    LifecycleSimulation simulation;
    simulation.disconnected = true;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::AttachLifecycle(*handle, simulation);
    handle->Stop();
    EXPECT_EQ(simulation.events.back(), "close");
    EXPECT_EQ(simulation.events.size(), 4U);
    EXPECT_LE(simulation.state_timeout, 500000);
}

TEST(EthercatLifecycleTests, FailedInitializationDoesNotPromoteSlavesToSafeOp) {
    LifecycleSimulation simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::AttachLifecycle(*handle, simulation, false);
    handle->Stop();
    EXPECT_EQ(simulation.events, (std::vector<std::string>{"close"}));
}

TEST(EthercatLifecycleTests, DefaultWatchdogEnablesAllOutputSlavesAndPreservesDividerAndSmFlags) {
    LifecycleSimulation simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::AttachLifecycle(*handle, simulation, false);
    ASSERT_TRUE(EthercatHandleTestAccess::ConfigureWatchdogs(*handle));
    for (uint16 slave = 1; slave <= 2; ++slave) {
        EXPECT_EQ(simulation.Get(slave, 0x0400), 2498);
        EXPECT_EQ(simulation.Get(slave, 0x0420), 1000);
        EXPECT_EQ(simulation.Get(slave, 0x0814), 0x64);
        EXPECT_EQ(simulation.Get(slave, 0x0816), 1);
    }
    EXPECT_EQ(EthercatHandleTestAccess::SmFlags(*handle), 0x10064U);
}

TEST(EthercatLifecycleTests, InputOnlySlavesAreNotConfigured) {
    LifecycleSimulation simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::AttachLifecycle(*handle, simulation, false);
    EthercatHandleTestAccess::SetInputOnly(*handle, 2);
    ASSERT_TRUE(EthercatHandleTestAccess::ConfigureWatchdogs(*handle));
    EXPECT_EQ(simulation.Get(1, 0x0420), 1000);
    EXPECT_EQ(simulation.Get(2, 0x0420), 0);
    EXPECT_EQ(simulation.Get(2, 0x0814), 0x24);
}

TEST(EthercatLifecycleTests, WatchdogOptionIsRemovedFromTimingEnvelopeAndRoundsUp) {
    LifecycleSimulation simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::AttachLifecycle(*handle, simulation, false);
    auto timing = EthercatHandleTestAccess::ParseOptions(
        *handle,
        R"({"watchdog_timeout_ms":123,"bitrate":500000,"buses":{"0:1":{"sample_point":0.8}}})");
    EXPECT_FALSE(timing.Common().contains("watchdog_timeout_ms"));
    EXPECT_EQ(timing.Scope(0, 1).at("bitrate"), 500000);
    EXPECT_EQ(timing.Scope(0, 1).at("sample_point"), 0.8);
    simulation.Set(1, 0x0400, 3000);
    ASSERT_TRUE(EthercatHandleTestAccess::ConfigureWatchdogs(*handle));
    EXPECT_EQ(simulation.Get(1, 0x0420), 1025);
    EXPECT_EQ(simulation.Get(2, 0x0420), 1230);
}

TEST(EthercatLifecycleTests, InvalidWatchdogOptionsAreRejected) {
    for (const std::string value :
         {"0", "-1", "60001", "1.5", "true", "null", "\"100\"", "18446744073709551615"}) {
        auto handle = EthercatHandleTestAccess::Create();
        EXPECT_THROW(EthercatHandleTestAccess::ParseOptions(
                         *handle, "{\"watchdog_timeout_ms\":" + value + "}"),
                     std::runtime_error);
    }
    auto handle = EthercatHandleTestAccess::Create();
    EXPECT_THROW(EthercatHandleTestAccess::ParseOptions(
                     *handle, R"({"buses":{"0:1":{"watchdog_timeout_ms":100}}})"),
                 std::runtime_error);
}

TEST(EthercatLifecycleTests, WatchdogReadWriteFailuresAndReadbackMismatchAreRejected) {
    unsigned operations = 0;
    {
        LifecycleSimulation simulation;
        auto handle = EthercatHandleTestAccess::Create();
        EthercatHandleTestAccess::AttachLifecycle(*handle, simulation, false);
        ASSERT_TRUE(EthercatHandleTestAccess::ConfigureWatchdogs(*handle));
        operations = simulation.operations;
    }
    for (unsigned failure = 1; failure <= operations; ++failure) {
        LifecycleSimulation simulation;
        simulation.fail_operation = failure;
        auto handle = EthercatHandleTestAccess::Create();
        EthercatHandleTestAccess::AttachLifecycle(*handle, simulation, false);
        EXPECT_FALSE(EthercatHandleTestAccess::ConfigureWatchdogs(*handle)) << failure;
    }
    LifecycleSimulation simulation;
    simulation.discard_writes = true;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::AttachLifecycle(*handle, simulation, false);
    EXPECT_FALSE(EthercatHandleTestAccess::ConfigureWatchdogs(*handle));
}

TEST(EthercatLifecycleTests, MissingOutputSmAndUnrepresentableTimeoutAreRejected) {
    LifecycleSimulation simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::AttachLifecycle(*handle, simulation, false);
    EthercatHandleTestAccess::RemoveOutputSm(*handle);
    EXPECT_FALSE(EthercatHandleTestAccess::ConfigureWatchdogs(*handle));
    EthercatHandleTestAccess::AttachLifecycle(*handle, simulation, false);
    simulation.Set(1, 0x0400, 0);
    EXPECT_FALSE(EthercatHandleTestAccess::ConfigureWatchdogs(*handle));
}

TEST(EthercatLifecycleTests, ShutdownUsesOneDeadlineAcrossUnresponsiveSlaves) {
    LifecycleSimulation simulation;
    simulation.disconnected = true;
    simulation.stall_state_check = true;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::AttachLifecycle(*handle, simulation);
    handle->Stop();
    EXPECT_EQ(simulation.events, (std::vector<std::string>{"request", "check1", "close"}));
}

TEST(EthercatLifecycleTests,
     AlreadyEnabledWatchdogPreservesSmAndOnlyOptionDoesNotCreateTimingSdos) {
    LifecycleSimulation simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::AttachLifecycle(*handle, simulation, false);
    simulation.registers[{1, 0x0814}] = {0x64};
    const auto timing =
        EthercatHandleTestAccess::ParseOptions(*handle, R"({"watchdog_timeout_ms":200})");
    EXPECT_TRUE(timing.Empty());
    ASSERT_TRUE(EthercatHandleTestAccess::ConfigureWatchdogs(*handle));
    EXPECT_EQ(simulation.Get(1, 0x0420), 2000);
    EXPECT_EQ(simulation.Get(1, 0x0814), 0x64);
    EXPECT_EQ(simulation.Get(1, 0x0816), 1);
}

TEST(EthercatLifecycleTests, RecoveryRestoresWatchdogAfterSlaveResetAndStopsOnFailure) {
    LifecycleSimulation simulation;
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::AttachLifecycle(*handle, simulation, false);
    EthercatHandleTestAccess::ParseOptions(*handle, R"({"watchdog_timeout_ms":200})");
    ASSERT_TRUE(EthercatHandleTestAccess::ConfigureWatchdogs(*handle));
    simulation.Set(1, 0x0400, 4998);
    simulation.Set(1, 0x0420, 0);
    simulation.registers[{1, 0x0814}] = {0x24};
    simulation.Set(2, 0x0420, 777);
    EXPECT_EQ(EthercatHandleTestAccess::RestoreWatchdog(*handle, 1), 1);
    EXPECT_EQ(simulation.Get(1, 0x0420), 1000);
    EXPECT_EQ(simulation.Get(1, 0x0814), 0x64);
    EXPECT_EQ(simulation.Get(2, 0x0420), 777);
    EXPECT_TRUE(EthercatHandleTestAccess::Running(*handle));
    simulation.fail_operation = simulation.operations + 1;
    EXPECT_EQ(EthercatHandleTestAccess::RestoreWatchdog(*handle, 1), 0);
    EXPECT_FALSE(EthercatHandleTestAccess::Running(*handle));
    EXPECT_FALSE(handle->Ok());
    EthercatHandleTestAccess::Check(*handle);
    EXPECT_TRUE(simulation.events.empty());
}

TEST(EthercatTimingTests, CommonTimingWritesOnlyActualChannelsOnMixedSlaves) {
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::ConfigureTimingSlaves(*handle, {2, 3, 8},
                                                    R"({"bitrate":500000,"sample_point":0.8})");
    ASSERT_TRUE(EthercatHandleTestAccess::ApplyTimings(*handle));
    std::vector<std::pair<uint16, uint16>> expected;
    const std::vector<unsigned> counts{2, 3, 8};
    for (unsigned slave = 0; slave < counts.size(); ++slave) {
        for (unsigned channel = 0; channel < counts[slave]; ++channel) {
            for (int field = 0; field < 5; ++field) {
                expected.emplace_back(slave + 1, 0x2300 + channel);
            }
        }
    }
    EXPECT_EQ(timing_writes, expected);
}

TEST(EthercatTimingTests, InvalidExplicitChannelIsRejectedBeforeAnyWrite) {
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::ConfigureTimingSlaves(
        *handle, {8, 2}, R"({"bitrate":500000,"buses":{"1:2":{"sample_point":0.8}}})");
    EXPECT_FALSE(EthercatHandleTestAccess::ApplyTimings(*handle));
    EXPECT_TRUE(timing_writes.empty());
}

TEST(EthercatTimingTests, UnconfiguredChannelsAreUntouched) {
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::ConfigureTimingSlaves(*handle, {2, 3}, "{}");
    EXPECT_TRUE(EthercatHandleTestAccess::ApplyTimings(*handle));
    EXPECT_TRUE(timing_writes.empty());
    EthercatHandleTestAccess::ConfigureTimingSlaves(*handle, {2, 3},
                                                    R"({"buses":{"1:2":{"sample_point":0.8}}})");
    EXPECT_TRUE(EthercatHandleTestAccess::ApplyTimings(*handle));
    EXPECT_EQ(timing_writes, (std::vector<std::pair<uint16, uint16>>(5, {2, 0x2302})));
}

TEST(EthercatTimingTests, RuntimeUpdateRejectsActualChannelBoundaryWithoutQueueing) {
    for (const unsigned count : {2U, 3U, 8U}) {
        auto handle = EthercatHandleTestAccess::Create();
        EthercatHandleTestAccess::ConfigureTimingSlaves(*handle, {count}, "{}");
        const auto extend = AdapterExtend::Parse("{\"buses\":{\"0:" + std::to_string(count) +
                                                 "\":{\"bitrate\":500000}}}");
        auto result = std::async(std::launch::async, [&] {
            return handle->ReconfigureBuses(extend);
        });
        EXPECT_EQ(result.wait_for(std::chrono::milliseconds(250)), std::future_status::ready);
        handle->RequestStop();
        EXPECT_THROW(result.get(), std::runtime_error);
        EXPECT_TRUE(timing_writes.empty());
    }
}

TEST(EthercatTimingTests, UnknownSlaveWithTimingFailsBeforeConfiguringEarlierSlaves) {
    auto handle = EthercatHandleTestAccess::Create();
    EthercatHandleTestAccess::ConfigureTimingSlaves(*handle, {8, 0}, R"({"bitrate":500000})");
    EXPECT_THROW(EthercatHandleTestAccess::ApplyTimings(*handle), std::runtime_error);
    EXPECT_TRUE(timing_writes.empty());
    EthercatHandleTestAccess::ConfigureTimingSlaves(*handle, {8, 0}, "{}");
    EXPECT_TRUE(EthercatHandleTestAccess::ApplyTimings(*handle));
    EXPECT_TRUE(timing_writes.empty());
}

TEST(EthercatTimingTests, SharedPlanUsesActualCountAndPreservesPerBusOverrides) {
    const auto extend = AdapterExtend::Parse(
        R"({"bitrate":500000,"sample_point":0.8,"buses":{"1:1":{"bitrate":250000}}})");
    for (const unsigned count : {2U, 3U, 8U}) {
        const auto plan = encos::ethercat::BuildChannelTimingPlan(extend, 1, count);
        ASSERT_EQ(plan.size(), count);
        for (unsigned bus = 0; bus < count; ++bus) {
            ASSERT_TRUE(plan[bus].has_value());
            EXPECT_EQ(plan[bus]->nominal_bps, bus == 1 ? 250000U : 500000U);
            EXPECT_DOUBLE_EQ(plan[bus]->nominal_sample_point, 0.8);
        }
    }
    EXPECT_THROW(encos::ethercat::BuildChannelTimingPlan(extend, 1, 0), std::runtime_error);
    EXPECT_THROW(encos::ethercat::BuildChannelTimingPlan(extend, 1, 9), std::runtime_error);
    EXPECT_THROW(encos::ethercat::BuildChannelTimingPlan(
                     AdapterExtend::Parse(R"({"buses":{"0:2":{"bitrate":500000}}})"), 0, 2),
                 std::runtime_error);
}
