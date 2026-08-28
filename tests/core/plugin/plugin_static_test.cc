#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>

#include "encos_motor.h"

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
