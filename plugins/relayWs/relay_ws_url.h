#pragma once

#include <optional>
#include <string>

namespace encos {

/**
 * @brief 解析后的 RelayWs /start URL 组成部分
 */
struct RelayWsUrlParts {
    std::string scheme;
    std::string host;
    int port = 0;
    std::string path;
    std::string query;
};

/**
 * @brief 对 URL 编码的字符串进行解码
 * @param value 编码后的字符串
 * @return 解码后的字符串
 */
std::string UrlDecode(const std::string& value);

/**
 * @brief 从查询字符串中获取指定键的值
 * @param query URL 查询字符串（不含前导 ?）
 * @param key 要查找的键
 * @return 键对应的值，不存在时返回空字符串
 */
std::string GetQueryValue(const std::string& query, const std::string& key);

/**
 * @brief 解析 RelayWs 的 /start URL
 * @param url 待解析的 URL
 * @return 解析成功返回组成部分，否则返回空
 */
std::optional<RelayWsUrlParts> ParseRelayWsStartUrl(const std::string& url);

/**
 * @brief 解析后的 RelayWs /start 响应
 */
struct RelayWsStartResponse {
    std::string session;
    int bus_count = 0;
};

/**
 * @brief 从 /start 的 JSON 响应中提取 session id 和 bus_count
 * @param response HTTP 响应体
 * @return 解析结果，提取失败时 session 为空
 */
RelayWsStartResponse ParseRelayStartResponse(const std::string& response);

}  // namespace encos
