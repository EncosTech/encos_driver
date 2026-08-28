#include <atomic>
#include <condition_variable>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#include "adapter/base_adapter.h"
#include "bus/bus.h"
#include "encos/driver_manager.h"
#include "motor/motor.h"

namespace encos {
namespace {

struct OwnershipProbeState {
    std::atomic<int> destructor_count{0};
    std::atomic<bool> route_retired_before_destructor{false};
    std::atomic<bool> worker_joined{false};
};

class OwnershipProbeAdapter final : public BaseAdapter {
public:
    OwnershipProbeAdapter(const std::string& interface_name,
                          std::shared_ptr<OwnershipProbeState> state, bool run_worker = false)
        : BaseAdapter(interface_name, "OwnershipProbe", LogLevel::Info), state_(std::move(state)) {
        if (!run_worker) {
            return;
        }
        worker_ = std::thread([this]() {
            std::unique_lock<std::mutex> lock(worker_mutex_);
            worker_condition_.wait(lock, [this]() {
                return stop_worker_;
            });
        });
    }

    ~OwnershipProbeAdapter() override {
        MotorPackMsg message{};
        message.id = 0x321;
        state_->route_retired_before_destructor.store(
            !EncosDriverManager::Instance().DispatchReceive(this, 0, message));
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            stop_worker_ = true;
        }
        worker_condition_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        state_->worker_joined.store(!worker_.joinable());
        ++state_->destructor_count;
    }

    std::unordered_map<int, Bus*> GetBuses() override {
        return GetKnownBusesSnapshot();
    }

    bool Ok() override {
        return true;
    }

protected:
    void Send(const MotorMessage& message) override {
        (void) message;
    }

private:
    std::shared_ptr<OwnershipProbeState> state_;
    std::mutex worker_mutex_;
    std::condition_variable worker_condition_;
    bool stop_worker_ = false;
    std::thread worker_;
};

TEST(PluginOwnershipTests, ManagerAdoptsExactlyOnceAndRollsBackInvalidFactoryResults) {
    auto& manager = EncosDriverManager::Instance();
    auto state = std::make_shared<OwnershipProbeState>();

    EXPECT_THROW(manager.CreateAdapterWithFactory("plugin-null-factory",
                                                  []() -> BaseAdapter* {
                                                      return nullptr;
                                                  }),
                 std::runtime_error);
    EXPECT_FALSE(manager.DestroyAdapterByInterfaceName("plugin-null-factory"));

    EXPECT_THROW(manager.CreateAdapterWithFactory("plugin-rollback",
                                                  [state]() {
                                                      return new OwnershipProbeAdapter(
                                                          "wrong-interface", state);
                                                  }),
                 std::invalid_argument);
    EXPECT_EQ(state->destructor_count.load(), 1);
    EXPECT_FALSE(manager.DestroyAdapterByInterfaceName("plugin-rollback"));

    int factory_calls = 0;
    auto factory = [state, &factory_calls]() {
        ++factory_calls;
        return new OwnershipProbeAdapter("plugin-owned", state);
    };
    auto* adapter = manager.CreateAdapterWithFactory("plugin-owned", factory);
    ASSERT_NE(adapter, nullptr);
    EXPECT_EQ(manager.CreateAdapterWithFactory("plugin-owned", factory), adapter);
    EXPECT_EQ(factory_calls, 1);
    EXPECT_EQ(state->destructor_count.load(), 1);
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
    EXPECT_EQ(state->destructor_count.load(), 2);
    EXPECT_FALSE(manager.DestroyAdapter(adapter));
    EXPECT_EQ(state->destructor_count.load(), 2);
}

TEST(PluginOwnershipTests, AdapterCascadeRetiresRoutesBeforeStoppingAndJoiningWorker) {
    auto& manager = EncosDriverManager::Instance();
    auto state = std::make_shared<OwnershipProbeState>();
    auto* adapter = manager.CreateAdapterWithFactory("plugin-worker-lifecycle", [state]() {
        return new OwnershipProbeAdapter("plugin-worker-lifecycle", state, true);
    });
    ASSERT_NE(adapter, nullptr);
    auto* bus = manager.CreateBus(adapter, 0);
    ASSERT_NE(bus, nullptr);
    ASSERT_NE(manager.CreateMotor(bus, 0x321, MotorModel::EC_A4310_P2), nullptr);

    EXPECT_TRUE(manager.DestroyAdapter(adapter));
    EXPECT_TRUE(state->route_retired_before_destructor.load());
    EXPECT_TRUE(state->worker_joined.load());
    EXPECT_EQ(state->destructor_count.load(), 1);
}

}  // namespace
}  // namespace encos
