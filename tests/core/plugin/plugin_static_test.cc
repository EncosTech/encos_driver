// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>

#include "encos_motor.h"
#include "external/json.hpp"

TEST(StaticPluginTests, EnumeratesCompiledAdaptersWithoutFake) {
    auto plugins = encos::GetAvailableAdapterTypes();
    EXPECT_THAT(plugins, ::testing::Not(::testing::Contains("Fake")));
    for (const auto& plugin : plugins) {
        EXPECT_FALSE(plugin.empty());
    }
}

TEST(StaticPluginTests, SetPluginPathWarnsAndRemainsHarmless) {
    encos::SetPluginPath("/tmp/ignored-static-plugin-path");
    SUCCEED();
}

TEST(StaticPluginTests, LoadsFakeAdapterExplicitly) {
    encos::SetPluginPath("/tmp/ignored-static-plugin-path");
    auto adapter =
        encos::MakeAdapter("Fake", "static_fake_adapter", "fake_logger", encos::LogLevel::Info);

    ASSERT_NE(adapter, nullptr);
    EXPECT_TRUE(adapter->Ok());
    EXPECT_TRUE(encos::EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(StaticPluginTests, ExtendAwareStaticFactoryReceivesTheConfiguration) {
    // Slcan 声明了 ENCOS_PLUGIN_STATIC_FACTORY_EXTEND；非法位率在打开串口前就被拒绝，
    // 因此这条断言只能由"配置串确实送达插件"解释，无需硬件。
    try {
        (void) encos::MakeAdapter("Slcan", "static-slcan-unused-path", "slcan_extend_logger",
                                  encos::LogLevel::Off, nlohmann::json{{"bitrate", 300000}});
        FAIL() << "expected the invalid bitrate to be rejected";
    } catch (const std::runtime_error& error) {
        EXPECT_THAT(std::string(error.what()),
                    ::testing::HasSubstr("SLCAN does not support bitrate"));
    }
}

TEST(StaticPluginTests, CanStaticFactoryRejectsInvalidTimingBeforeTouchingTheInterface) {
    try {
        (void) encos::MakeAdapter("Can", "static-can-unused-interface", "can_extend_logger",
                                  encos::LogLevel::Off, nlohmann::json{{"sample_point", 1.5}});
        FAIL() << "expected the invalid sample point to be rejected";
    } catch (const std::runtime_error& error) {
        EXPECT_THAT(std::string(error.what()), ::testing::HasSubstr("CAN extend 'sample_point'"));
    }
}

TEST(StaticPluginTests, PluginWithoutExtendSupportIgnoresTheConfiguration) {
    // Fake 未声明 ENCOS_PLUGIN_STATIC_FACTORY_EXTEND：形参被忽略，构造行为不变
    auto* adapter = encos::MakeAdapter("Fake", "static_fake_extend_ignored", "fake_logger",
                                       encos::LogLevel::Info, nlohmann::json{{"bitrate", 500000}});
    ASSERT_NE(adapter, nullptr);
    EXPECT_TRUE(adapter->Ok());
    EXPECT_TRUE(encos::EncosDriverManager::Instance().DestroyAdapter(adapter));
}

TEST(StaticPluginTests, ExtendRejectsABusTheAdapterDoesNotHave) {
    // Can 只有总线 (0, 0)：该拒绝发生在打开接口之前，因此无需硬件
    const auto extend = nlohmann::json::parse(R"({"buses": {"1:0": {"bitrate": 500000}}})");
    try {
        (void) encos::MakeAdapter("Can", "static-can-unknown-bus", "can_extend_logger",
                                  encos::LogLevel::Off, extend);
        FAIL() << "expected the unknown bus to be rejected";
    } catch (const std::runtime_error& error) {
        EXPECT_THAT(std::string(error.what()), ::testing::HasSubstr("has no bus"));
    }
}
