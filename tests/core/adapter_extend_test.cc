// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "adapter/adapter_extend.h"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace encos {
namespace {

TEST(AdapterExtendTests, EmptyInputIsAnEmptyEnvelope) {
    const auto extend = AdapterExtend::Parse("");
    EXPECT_TRUE(extend.Empty());
    EXPECT_TRUE(extend.DeclaredBuses().empty());
    EXPECT_TRUE(extend.Scope(0, 0).empty());
}

TEST(AdapterExtendTests, EmptyObjectIsAnEmptyEnvelope) {
    const auto extend = AdapterExtend::Parse("{}");
    EXPECT_TRUE(extend.Empty());
    EXPECT_TRUE(extend.Scope(3, 7).empty());
}

TEST(AdapterExtendTests, TopLevelKeysFormTheCommonScope) {
    const auto extend = AdapterExtend::Parse(R"({"bitrate": 500000, "sample_point": 0.875})");
    EXPECT_FALSE(extend.Empty());
    const auto scope = extend.Scope(0, 0);
    EXPECT_EQ(scope.at("bitrate"), 500000);
    EXPECT_DOUBLE_EQ(scope.at("sample_point").get<double>(), 0.875);
    // 公共参数作用于每一条总线
    EXPECT_EQ(extend.Scope(2, 5).at("bitrate"), 500000);
}

TEST(AdapterExtendTests, PerBusOverridesAreFieldLevel) {
    const auto extend = AdapterExtend::Parse(
        R"({"bitrate": 1000000, "sample_point": 0.765, "buses": {"2:5": {"bitrate": 250000}}})");
    const auto overridden = extend.Scope(2, 5);
    EXPECT_EQ(overridden.at("bitrate"), 250000);
    // 未在该总线列出的字段沿用公共值
    EXPECT_DOUBLE_EQ(overridden.at("sample_point").get<double>(), 0.765);
    // 其它总线不受该覆盖影响
    EXPECT_EQ(extend.Scope(2, 4).at("bitrate"), 1000000);
    EXPECT_EQ(extend.DeclaredBuses().size(), 1u);
    EXPECT_EQ(extend.DeclaredBuses()[0], std::make_pair(2, 5));
}

TEST(AdapterExtendTests, BothBusIdSpellingsAddressTheSameBus) {
    const auto decimal = AdapterExtend::Parse(R"({"buses": {"65541": {"bitrate": 250000}}})");
    const auto colon = AdapterExtend::Parse(R"({"buses": {"1:5": {"bitrate": 250000}}})");
    ASSERT_EQ(decimal.DeclaredBuses().size(), 1u);
    ASSERT_EQ(colon.DeclaredBuses().size(), 1u);
    EXPECT_EQ(decimal.DeclaredBuses()[0], colon.DeclaredBuses()[0]);
    EXPECT_EQ(decimal.Scope(1, 5), colon.Scope(1, 5));
}

TEST(AdapterExtendTests, DuplicateBusAcrossSpellingsIsRejected) {
    EXPECT_THROW(AdapterExtend::Parse(R"({"buses": {"0": {}, "0:0": {}}})"), std::runtime_error);
    EXPECT_THROW(AdapterExtend::Parse(R"({"buses": {"65536": {}, "1:0": {}}})"),
                 std::runtime_error);
}

TEST(AdapterExtendTests, MalformedBusKeysAreRejected) {
    for (const char* key : {"", "01", "-1", "1:", ":1", "1:2:3", "a", " 1", "0x10", "+1"}) {
        const std::string text = std::string(R"({"buses": {")") + "\"" + key + "\": {}}}";
        EXPECT_THROW(AdapterExtend::Parse(text), std::runtime_error) << "key=" << key;
    }
}

TEST(AdapterExtendTests, OutOfRangeBusEncodingIsRejected) {
    // 裸索引拆解后 bus 超过 0xFF
    EXPECT_THROW(AdapterExtend::Parse(R"({"buses": {"70000": {}}})"), std::runtime_error);
    // 显式写法下同样校验编码上界
    EXPECT_THROW(AdapterExtend::Parse(R"({"buses": {"1:256": {}}})"), std::runtime_error);
    EXPECT_THROW(AdapterExtend::Parse(R"({"buses": {"131072:0": {}}})"), std::runtime_error);
}

TEST(AdapterExtendTests, NonObjectShapesAreRejected) {
    EXPECT_THROW(AdapterExtend::Parse("[1,2]"), std::runtime_error);
    EXPECT_THROW(AdapterExtend::Parse("42"), std::runtime_error);
    EXPECT_THROW(AdapterExtend::Parse("{"), std::runtime_error);
    EXPECT_THROW(AdapterExtend::Parse(R"({"buses": []})"), std::runtime_error);
    EXPECT_THROW(AdapterExtend::Parse(R"({"buses": {"0": 5}})"), std::runtime_error);
}

TEST(AdapterExtendTests, BusOverrideReturnsOnlyTheNamedBusAndSkipsCommon) {
    const auto extend = AdapterExtend::Parse(
        R"({"bitrate": 250000, "buses": {"2:5": {"sample_point": 0.9}, "7": {"bitrate": 500000}}})");

    // 顶层公共参数只经 Common() 暴露，不得混进任一条总线的覆盖条目——增量更新路径
    // 正是靠这一点避免把顶层参数重新应用到已存在的实例上。
    ASSERT_EQ(extend.Common().size(), 1U);
    EXPECT_EQ(extend.Common().at("bitrate"), 250000);

    const nlohmann::json* named = extend.BusOverride(2, 5);
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->size(), 1U);
    EXPECT_TRUE(named->contains("sample_point"));
    EXPECT_FALSE(named->contains("bitrate"));

    // 裸索引与 <slave>:<bus> 两种拼写等价
    const nlohmann::json* bare = extend.BusOverride(0, 7);
    ASSERT_NE(bare, nullptr);
    EXPECT_EQ(bare->at("bitrate"), 500000);

    // 未点名的总线没有覆盖条目
    EXPECT_EQ(extend.BusOverride(3, 3), nullptr);
    EXPECT_EQ(extend.BusOverride(2, 6), nullptr);
    EXPECT_EQ(extend.DeclaredBuses().size(), 2U);
}

TEST(AdapterExtendTests, BusOverrideSeesNothingWhenOnlyCommonIsGiven) {
    const auto extend = AdapterExtend::Parse(R"({"bitrate": 1000000})");
    EXPECT_FALSE(extend.Common().empty());
    EXPECT_EQ(extend.BusOverride(0, 0), nullptr);
    EXPECT_TRUE(extend.DeclaredBuses().empty());
}

TEST(AdapterExtendTests, EmptyEnvelopeHasNeitherCommonNorOverrides) {
    for (const char* text : {"", "{}"}) {
        const auto extend = AdapterExtend::Parse(text);
        EXPECT_TRUE(extend.Common().empty()) << "text=" << text;
        EXPECT_EQ(extend.BusOverride(0, 0), nullptr) << "text=" << text;
        EXPECT_TRUE(extend.Empty()) << "text=" << text;
    }
}

}  // namespace
}  // namespace encos
