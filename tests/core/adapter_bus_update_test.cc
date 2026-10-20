// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "adapter/base_adapter.h"
#include "bus/bus.h"
#include "encos/driver_manager.h"

namespace encos {
namespace {

/** @brief 记录 UpdateBusExtend 调用并可按需抛异常的探针状态 */
struct BusUpdateProbeState {
    std::atomic<int> updates{0};
    std::atomic<int> destructor_count{0};
    std::mutex mutex;
    std::vector<std::string> received;
    bool throw_on_update{false};
};

class BusUpdateProbeAdapter final : public BaseAdapter {
public:
    BusUpdateProbeAdapter(const std::string& interface_name,
                          std::shared_ptr<BusUpdateProbeState> state)
        : BaseAdapter(interface_name, "BusUpdateProbe", LogLevel::Info), state_(std::move(state)) {}

    ~BusUpdateProbeAdapter() override {
        ++state_->destructor_count;
    }

    std::unordered_map<int, Bus*> GetBuses() override {
        return GetKnownBusesSnapshot();
    }

    bool Ok() override {
        return true;
    }

    bool UpdateBusExtend(const std::string& extend_json) override {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->received.push_back(extend_json);
            if (state_->throw_on_update) {
                throw std::runtime_error("probe rejected the update");
            }
        }
        state_->updates.fetch_add(1);
        return true;
    }

protected:
    void Send(const MotorMessage& message) override {
        (void) message;
    }

private:
    std::shared_ptr<BusUpdateProbeState> state_;
};

constexpr const char* kProbeInterface = "adapter-bus-update-probe";

class AdapterBusUpdateTests : public ::testing::Test {
protected:
    void TearDown() override {
        EncosDriverManager::Instance().DestroyAdapterByInterfaceName(kProbeInterface);
    }
};

TEST_F(AdapterBusUpdateTests, CacheHitRoutesTheExtendToTheExistingInstance) {
    auto& manager = EncosDriverManager::Instance();
    auto state = std::make_shared<BusUpdateProbeState>();
    int factory_calls = 0;
    const auto factory = [&]() -> BaseAdapter* {
        ++factory_calls;
        return new BusUpdateProbeAdapter(kProbeInterface, state);
    };

    BaseAdapter* first = manager.CreateAdapterWithFactory(kProbeInterface, factory);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(factory_calls, 1);
    // 首次创建时扩展配置由工厂自己消费，不触发增量更新。
    EXPECT_EQ(state->updates.load(), 0);

    const std::string extend = R"({"buses":{"0:7":{"bitrate":500000}}})";
    BaseAdapter* second = manager.CreateAdapterWithFactory(kProbeInterface, extend, factory);
    EXPECT_EQ(second, first);
    EXPECT_EQ(factory_calls, 1); /* 命中缓存不重跑工厂 */
    EXPECT_EQ(state->updates.load(), 1);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        ASSERT_EQ(state->received.size(), 1U);
        // 宿主规范化后的配置串被原样转发，管理器不解释其内容。
        EXPECT_EQ(state->received.front(), extend);
    }
}

TEST_F(AdapterBusUpdateTests, EmptyExtendDoesNotTriggerAnUpdate) {
    auto& manager = EncosDriverManager::Instance();
    auto state = std::make_shared<BusUpdateProbeState>();
    const auto factory = [&]() -> BaseAdapter* {
        return new BusUpdateProbeAdapter(kProbeInterface, state);
    };

    ASSERT_NE(manager.CreateAdapterWithFactory(kProbeInterface, factory), nullptr);
    EXPECT_EQ(manager.CreateAdapterWithFactory(kProbeInterface, std::string{}, factory),
              manager.CreateAdapterWithFactory(kProbeInterface, factory));
    EXPECT_EQ(state->updates.load(), 0);
}

TEST_F(AdapterBusUpdateTests, RejectedUpdateLeavesTheExistingInstanceUsable) {
    auto& manager = EncosDriverManager::Instance();
    auto state = std::make_shared<BusUpdateProbeState>();
    const auto factory = [&]() -> BaseAdapter* {
        return new BusUpdateProbeAdapter(kProbeInterface, state);
    };

    BaseAdapter* first = manager.CreateAdapterWithFactory(kProbeInterface, factory);
    ASSERT_NE(first, nullptr);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->throw_on_update = true;
    }

    EXPECT_THROW(
        manager.CreateAdapterWithFactory(kProbeInterface, R"({"buses":{"0:7":{}}})", factory),
        std::runtime_error);
    // 更新失败不得撤销或销毁既有实例。
    EXPECT_TRUE(first->Ok());
    EXPECT_EQ(state->destructor_count.load(), 0);
    EXPECT_EQ(state->updates.load(), 0);
}

TEST_F(AdapterBusUpdateTests, DefaultImplementationReportsUnsupportedWithoutThrowing) {
    auto& manager = EncosDriverManager::Instance();
    // 不重写 UpdateBusExtend 的适配器走基类默认实现。
    class PlainAdapter final : public BaseAdapter {
    public:
        PlainAdapter() : BaseAdapter(kProbeInterface, "PlainProbe", LogLevel::Info) {}
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
    };

    BaseAdapter* first = manager.CreateAdapterWithFactory(kProbeInterface, []() -> BaseAdapter* {
        return new PlainAdapter();
    });
    ASSERT_NE(first, nullptr);
    EXPECT_FALSE(first->UpdateBusExtend(R"({"buses":{"0:7":{}}})"));
    // 空串表示“本次没有更新请求”，不算不支持。
    EXPECT_TRUE(first->UpdateBusExtend(std::string{}));
    EXPECT_EQ(manager.CreateAdapterWithFactory(kProbeInterface, R"({"buses":{"0:7":{}}})",
                                               []() -> BaseAdapter* {
                                                   return new PlainAdapter();
                                               }),
              first);
}

}  // namespace
}  // namespace encos
