// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "test_device.h"

#include <algorithm>
#include <cstdint>
#include <future>
#include <gtest/gtest.h>

#include "device/device.h"
#include "encos/driver_manager.h"
#include "encos/encos_driver.h"
#include "external/json.hpp"
#include "test_fixtures.h"

namespace encos {
namespace {

MotorMessage MakeExternalFrame(std::uint32_t id) {
    MotorMessage message{};
    message.bus_idx = 0;
    message.data.id = id;
    message.data.len = 3;
    message.data.data[0] = 0x11;
    message.data.data[1] = 0x22;
    message.data.data[2] = 0x33;
    return message;
}

class ExtendRecordingAdapter : public FakeAdapter {
public:
    using FakeAdapter::FakeAdapter;
    std::string last_extend;
    std::function<void()> on_update;
    bool UpdateBusExtend(const std::string& extend) override {
        if (on_update) {
            on_update();
        }
        last_extend = extend;
        return true;
    }
};

TEST(AdapterLoaderUpdateTest, ExistingAdapterReceivesBusConfiguration) {
    auto& manager = EncosDriverManager::Instance();
    const std::string name = "loader-update-test";
    auto* adapter =
        static_cast<ExtendRecordingAdapter*>(manager.CreateAdapterWithFactory(name, [&] {
            return new ExtendRecordingAdapter(name);
        }));
    const nlohmann::json extend{{"buses", {{"0:0", {{"bitrate", 250000}}}}}};
    EXPECT_EQ(manager.CreateAdapter("Fake", name, "", LogLevel::Off, extend), adapter);
    EXPECT_EQ(adapter->last_extend, extend.dump());
    adapter->last_extend.clear();
    EXPECT_EQ(manager.CreateAdapter("Fake", name, "", LogLevel::Off), adapter);
    EXPECT_TRUE(adapter->last_extend.empty());
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

TEST(AdapterLoaderUpdateTest, ConcurrentUpdatesAreSerialized) {
    auto& manager = EncosDriverManager::Instance();
    const std::string name = "serialized-update-test";
    auto* adapter =
        static_cast<ExtendRecordingAdapter*>(manager.CreateAdapterWithFactory(name, [&] {
            return new ExtendRecordingAdapter(name);
        }));
    std::promise<void> entered;
    std::promise<void> release;
    auto released = release.get_future().share();
    std::atomic<int> calls{0};
    adapter->on_update = [&] {
        if (calls.fetch_add(1) == 0) {
            entered.set_value();
            released.wait();
        }
    };
    auto update = [&] {
        return manager.CreateAdapterWithFactory(name, "{}", []() -> BaseAdapter* {
            throw std::runtime_error("existing adapter must be reused");
        });
    };
    auto first = std::async(std::launch::async, update);
    entered.get_future().wait();
    auto second = std::async(std::launch::async, update);
    EXPECT_EQ(second.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
    EXPECT_EQ(calls.load(), 1);
    release.set_value();
    EXPECT_EQ(first.get(), adapter);
    EXPECT_EQ(second.get(), adapter);
    EXPECT_EQ(calls.load(), 2);
    EXPECT_TRUE(manager.DestroyAdapter(adapter));
}

TEST(AdapterLoaderUpdateTest, DestructionWaitsForActiveUpdate) {
    auto& manager = EncosDriverManager::Instance();
    const std::string name = "update-destruction-test";
    auto* adapter =
        static_cast<ExtendRecordingAdapter*>(manager.CreateAdapterWithFactory(name, [&] {
            return new ExtendRecordingAdapter(name);
        }));
    std::promise<void> entered;
    std::promise<void> release;
    auto released = release.get_future().share();
    adapter->on_update = [&] {
        entered.set_value();
        released.wait();
    };
    auto update = std::async(std::launch::async, [&] {
        manager.CreateAdapterWithFactory(name, "{}", [] {
            return nullptr;
        });
    });
    entered.get_future().wait();
    auto destroy = std::async(std::launch::async, [&] {
        return manager.DestroyAdapter(adapter);
    });
    EXPECT_EQ(destroy.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
    release.set_value();
    EXPECT_NO_THROW(update.get());
    EXPECT_TRUE(destroy.get());
}

TEST_F(MotorTestFixture, ExternalDeviceReceivesFramesAndIsReused) {
    Device* device = bus->GetDevice("TestDevice");
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->Type(), "TestDevice");
    EXPECT_EQ(device->Config(), "{}");
    EXPECT_EQ(bus->GetDevice("TestDevice"), device);
    EXPECT_EQ(bus->GetDevice("TestDevice", nlohmann::json::object()), device);

    auto* test_device = dynamic_cast<TestDevice*>(device);
    ASSERT_NE(test_device, nullptr);

    adapter->InjectMessage(MakeExternalFrame(0x7A0u));

    EXPECT_EQ(test_device->UpdateCount(), 1);
    ASSERT_TRUE(test_device->LastMessage().has_value());
    EXPECT_EQ(test_device->LastMessage()->id, 0x7A0u);

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyDevice(device));
}

TEST_F(MotorTestFixture, ExternalDeviceConfigSelectsDistinctRoute) {
    const nlohmann::json zero_config{{"base_id", 0x7A0u}};
    const nlohmann::json one_config{{"base_id", 0x7A1u}};
    Device* device_zero = bus->GetDevice("TestDevice", zero_config);
    Device* device_one = bus->GetDevice("TestDevice", one_config);
    ASSERT_NE(device_zero, nullptr);
    ASSERT_NE(device_one, nullptr);
    EXPECT_NE(device_zero, device_one);
    // 配置不同即独立实例；相同配置复用既有实例。
    EXPECT_EQ(bus->GetDevice("TestDevice", one_config), device_one);

    auto* typed_zero = dynamic_cast<TestDevice*>(device_zero);
    auto* typed_one = dynamic_cast<TestDevice*>(device_one);
    ASSERT_NE(typed_zero, nullptr);
    ASSERT_NE(typed_one, nullptr);

    adapter->InjectMessage(MakeExternalFrame(0x7A1u));

    EXPECT_EQ(typed_zero->UpdateCount(), 0);
    EXPECT_EQ(typed_one->UpdateCount(), 1);

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyDevice(device_zero));
    EXPECT_TRUE(EncosDriverManager::Instance().DestroyDevice(device_one));
}

TEST_F(MotorTestFixture, ExternalDeviceWithoutRoutesIsRejected) {
    EXPECT_THROW(bus->GetDevice("MissingDevice"), std::runtime_error);
}

TEST_F(MotorTestFixture, AvailableDeviceTypesIncludesTestDevice) {
    const auto types = GetAvailableDeviceTypes();
    EXPECT_NE(std::find(types.begin(), types.end(), "TestDevice"), types.end());
}

class ExternalDeviceCascadeFixture : public ::testing::Test {
protected:
    void SetUp() override {
        platform::SetEnv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").string());
        adapter_ =
            static_cast<FakeAdapter*>(EncosDriverManager::Instance().CreateAdapterWithFactory(
                "ExternalDeviceCascadeAdapter", []() {
                    return new FakeAdapter("ExternalDeviceCascadeAdapter");
                }));
        bus_ = adapter_->GetBus(0);
    }

    FakeAdapter* adapter_ = nullptr;
    Bus* bus_ = nullptr;
};

TEST_F(ExternalDeviceCascadeFixture, AdapterCascadeDestroysExternalDevice) {
    Device* device = bus_->GetDevice("TestDevice");
    ASSERT_NE(device, nullptr);
    auto* typed = dynamic_cast<TestDevice*>(device);
    ASSERT_NE(typed, nullptr);

    adapter_->InjectMessage(MakeExternalFrame(0x7A0u));
    EXPECT_EQ(typed->UpdateCount(), 1);

    EXPECT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter_));
}

}  // namespace
}  // namespace encos
