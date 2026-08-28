#include <chrono>
#include <filesystem>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>
#include <type_traits>

#include "encos_motor.h"

namespace fs = std::filesystem;

static_assert(std::is_same_v<decltype(&encos::MakeAdapter),
                             encos::BaseAdapter* (*) (const std::string&, const std::string&,
                                                      const std::string&, encos::LogLevel)>);
static_assert(std::is_same_v<decltype(&encos::DeleteAdapter), bool (*)(encos::BaseAdapter*)>);

TEST(PluginTests, enum_plugin) {
    setenv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").c_str(), 1);

    auto plugins = encos::GetAvailableAdapterTypes();
    EXPECT_THAT(plugins, ::testing::Not(::testing::Contains("Fake")));
    EXPECT_THAT(plugins, ::testing::Contains("PluginCacheTest"));
    for (const auto& plugin : plugins) {
        EXPECT_FALSE(plugin.empty());
    }
}

TEST(PluginTests, MakeAdapterReusesAdapterByName) {
    setenv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").c_str(), 1);

    auto first = encos::MakeAdapter("PluginCacheTest", "cached_adapter", "first_logger",
                                    encos::LogLevel::Debug);
    auto second = encos::MakeAdapter("PluginCacheTest", "cached_adapter", "second_logger",
                                     encos::LogLevel::Error);

    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_TRUE(encos::DeleteAdapter(first));
}

TEST(PluginTests, MakeAdapterLoadsFakePluginExplicitly) {
    setenv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").c_str(), 1);

    auto adapter =
        encos::MakeAdapter("Fake", "plugin_fake_adapter", "fake_logger", encos::LogLevel::Info);

    ASSERT_NE(adapter, nullptr);
    EXPECT_TRUE(adapter->Ok());
    EXPECT_TRUE(encos::DeleteAdapter(adapter));
}

TEST(PluginTests, ManagerRollsBackInvalidDynamicFactoryResults) {
    setenv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").c_str(), 1);
    auto& manager = encos::EncosDriverManager::Instance();

    EXPECT_THROW(encos::MakeAdapter("PluginCacheTest", "plugin-null-result"), std::runtime_error);
    EXPECT_FALSE(manager.DestroyAdapterByInterfaceName("plugin-null-result"));

    EXPECT_THROW(encos::MakeAdapter("PluginCacheTest", "plugin-identity-mismatch"),
                 std::invalid_argument);
    EXPECT_FALSE(manager.DestroyAdapterByInterfaceName("plugin-identity-mismatch"));
}
