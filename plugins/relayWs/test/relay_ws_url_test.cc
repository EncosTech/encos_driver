#include "relayWs/relay_ws_url.h"

#include <gtest/gtest.h>

namespace encos {

TEST(RelayWsUrlTest, ParsesHttpUrlWithPortAndQuery) {
    const auto parsed = ParseRelayWsStartUrl(
        "http://192.168.1.10:9001/start?token=abc&AdapterType=Fake&AdapterName=fake0");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->scheme, "http");
    EXPECT_EQ(parsed->host, "192.168.1.10");
    EXPECT_EQ(parsed->port, 9001);
    EXPECT_EQ(parsed->path, "/start");
    EXPECT_EQ(parsed->query, "token=abc&AdapterType=Fake&AdapterName=fake0");
}

TEST(RelayWsUrlTest, ParsesHttpsUrlWithDefaultPort) {
    const auto parsed = ParseRelayWsStartUrl("https://example.com/start?token=abc");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->scheme, "https");
    EXPECT_EQ(parsed->host, "example.com");
    EXPECT_EQ(parsed->port, 443);
    EXPECT_EQ(parsed->path, "/start");
}

TEST(RelayWsUrlTest, ParsesUrlWithoutPath) {
    const auto parsed = ParseRelayWsStartUrl("http://localhost:8080");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->host, "localhost");
    EXPECT_EQ(parsed->port, 8080);
    EXPECT_EQ(parsed->path, "/");
    EXPECT_TRUE(parsed->query.empty());
}

TEST(RelayWsUrlTest, RejectsMalformedUrl) {
    EXPECT_FALSE(ParseRelayWsStartUrl("not-a-url").has_value());
    EXPECT_FALSE(ParseRelayWsStartUrl("").has_value());
}

TEST(RelayWsUrlTest, ExtractsQueryValue) {
    EXPECT_EQ(GetQueryValue("token=abc&AdapterType=Fake", "token"), "abc");
    EXPECT_EQ(GetQueryValue("token=abc&AdapterType=Fake", "AdapterType"), "Fake");
    EXPECT_EQ(GetQueryValue("token=abc&AdapterType=Fake", "missing"), "");
}

TEST(RelayWsUrlTest, UrlDecodesQueryValue) {
    EXPECT_EQ(GetQueryValue("name=hello%20world", "name"), "hello world");
    EXPECT_EQ(GetQueryValue("name=hello+world", "name"), "hello world");
}

TEST(RelayWsUrlTest, ExtractsSessionAndBusCountFromJsonResponse) {
    {
        const auto response = ParseRelayStartResponse(R"({"session":"abcd1234"})");
        EXPECT_EQ(response.session, "abcd1234");
        EXPECT_EQ(response.bus_count, 0);
    }
    {
        const auto response = ParseRelayStartResponse(R"({"other":"x","session":"sess"})");
        EXPECT_EQ(response.session, "sess");
        EXPECT_EQ(response.bus_count, 0);
    }
    {
        const auto response = ParseRelayStartResponse(R"({"session":"sess","bus_count":3})");
        EXPECT_EQ(response.session, "sess");
        EXPECT_EQ(response.bus_count, 3);
    }
    {
        const auto response = ParseRelayStartResponse("invalid");
        EXPECT_TRUE(response.session.empty());
        EXPECT_EQ(response.bus_count, 0);
    }
    {
        const auto response = ParseRelayStartResponse("");
        EXPECT_TRUE(response.session.empty());
        EXPECT_EQ(response.bus_count, 0);
    }
}

}  // namespace encos
