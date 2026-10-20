// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <chrono>
#include <filesystem>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>
#include <type_traits>

#include "encos_motor.h"
#include "platform/os.h"

namespace fs = std::filesystem;

using MakeAdapterFunction = encos::BaseAdapter* (*) (const std::string&, const std::string&,
                                                     const std::string&, encos::LogLevel);
using MakeAdapterExtendFunction = encos::BaseAdapter* (*) (const std::string&, const std::string&,
                                                           const std::string&, encos::LogLevel,
                                                           const nlohmann::json&);
// MakeAdapter 已重载：分别校验两个重载都保持精确签名
static_assert(std::is_same_v<decltype(static_cast<MakeAdapterFunction>(&encos::MakeAdapter)),
                             MakeAdapterFunction>);
static_assert(std::is_same_v<decltype(static_cast<MakeAdapterExtendFunction>(&encos::MakeAdapter)),
                             MakeAdapterExtendFunction>);
static_assert(std::is_same_v<decltype(&encos::DeleteAdapter), bool (*)(encos::BaseAdapter*)>);

TEST(PluginTests, enum_plugin) {
    encos::platform::SetEnv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").string());

    auto plugins = encos::GetAvailableAdapterTypes();
    EXPECT_THAT(plugins, ::testing::Not(::testing::Contains("Fake")));
    EXPECT_THAT(plugins, ::testing::Contains("PluginCacheTest"));
    for (const auto& plugin : plugins) {
        EXPECT_FALSE(plugin.empty());
    }
}

TEST(PluginTests, MakeAdapterReusesAdapterByName) {
    encos::platform::SetEnv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").string());

    auto first = encos::MakeAdapter("PluginCacheTest", "cached_adapter", "first_logger",
                                    encos::LogLevel::Debug);
    auto second = encos::MakeAdapter("PluginCacheTest", "cached_adapter", "second_logger",
                                     encos::LogLevel::Error);

    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_TRUE(encos::DeleteAdapter(first));
}

TEST(PluginTests, MakeAdapterLoadsFakePluginExplicitly) {
    encos::platform::SetEnv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").string());

    auto adapter =
        encos::MakeAdapter("Fake", "plugin_fake_adapter", "fake_logger", encos::LogLevel::Info);

    ASSERT_NE(adapter, nullptr);
    EXPECT_TRUE(adapter->Ok());
    EXPECT_TRUE(encos::DeleteAdapter(adapter));
}

TEST(PluginTests, ManagerRollsBackInvalidDynamicFactoryResults) {
    encos::platform::SetEnv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").string());
    auto& manager = encos::EncosDriverManager::Instance();

    EXPECT_THROW(encos::MakeAdapter("PluginCacheTest", "plugin-null-result"), std::runtime_error);
    EXPECT_FALSE(manager.DestroyAdapterByInterfaceName("plugin-null-result"));

    EXPECT_THROW(encos::MakeAdapter("PluginCacheTest", "plugin-identity-mismatch"),
                 std::invalid_argument);
    EXPECT_FALSE(manager.DestroyAdapterByInterfaceName("plugin-identity-mismatch"));
}

TEST(PluginTests, ExtendMustBeAJsonObject) {
    encos::platform::SetEnv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").string());

    EXPECT_THROW(encos::MakeAdapter("PluginCacheTest", "extend_array", "logger",
                                    encos::LogLevel::Info, nlohmann::json::array()),
                 std::runtime_error);
    EXPECT_THROW(encos::MakeAdapter("PluginCacheTest", "extend_scalar", "logger",
                                    encos::LogLevel::Info, nlohmann::json(7)),
                 std::runtime_error);
    // 抛出后不得发布适配器：同名接口随后仍可用空 extend 正常创建
    auto* adapter =
        encos::MakeAdapter("PluginCacheTest", "extend_array", "logger", encos::LogLevel::Info);
    ASSERT_NE(adapter, nullptr);
    EXPECT_TRUE(encos::DeleteAdapter(adapter));
}

TEST(PluginTests, EmptyExtendIsEquivalentToTheFourArgumentEntry) {
    encos::platform::SetEnv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").string());

    auto* legacy =
        encos::MakeAdapter("PluginCacheTest", "extend_legacy", "logger", encos::LogLevel::Info);
    auto* explicit_empty = encos::MakeAdapter("PluginCacheTest", "extend_explicit_empty", "logger",
                                              encos::LogLevel::Info, nlohmann::json::object());

    ASSERT_NE(legacy, nullptr);
    ASSERT_NE(explicit_empty, nullptr);
    EXPECT_TRUE(encos::DeleteAdapter(legacy));
    EXPECT_TRUE(encos::DeleteAdapter(explicit_empty));
}

TEST(PluginTests, CacheHitIgnoresALaterExtend) {
    encos::platform::SetEnv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").string());

    auto* first =
        encos::MakeAdapter("PluginCacheTest", "extend_cache", "logger", encos::LogLevel::Info);
    ASSERT_NE(first, nullptr);

    // 适配器身份仍只由 interface_name 决定：命中缓存时新的 extend 不生效
    auto* second = encos::MakeAdapter("PluginCacheTest", "extend_cache", "other_logger",
                                      encos::LogLevel::Error, nlohmann::json{{"bitrate", 500000}});
    EXPECT_EQ(first, second);
    EXPECT_TRUE(encos::DeleteAdapter(first));
}

TEST(PluginTests, PluginWithoutMakeAdapterV2StillConstructsWithExtend) {
    encos::platform::SetEnv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").string());

    // PluginCacheTest 只导出 MakeAdapter：宿主回退到旧入口并记录不支持 extend 的诊断
    auto* adapter = encos::MakeAdapter("PluginCacheTest", "extend_unsupported", "logger",
                                       encos::LogLevel::Info, nlohmann::json{{"bitrate", 500000}});
    ASSERT_NE(adapter, nullptr);
    EXPECT_TRUE(encos::DeleteAdapter(adapter));
}
