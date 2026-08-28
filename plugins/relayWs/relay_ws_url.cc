#include "relayWs/relay_ws_url.h"

#include <cstdlib>

namespace encos {

std::string UrlDecode(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hex = std::stoi(value.substr(i + 1, 2), nullptr, 16);
            result.push_back(static_cast<char>(hex));
            i += 2;
        } else if (value[i] == '+') {
            result.push_back(' ');
        } else {
            result.push_back(value[i]);
        }
    }
    return result;
}

std::string GetQueryValue(const std::string& query, const std::string& key) {
    const std::string key_eq = key + "=";
    std::size_t pos = query.find(key_eq);
    if (pos == std::string::npos) {
        return "";
    }
    pos += key_eq.size();
    std::size_t end = query.find('&', pos);
    if (end == std::string::npos) {
        end = query.size();
    }
    return UrlDecode(query.substr(pos, end - pos));
}

std::optional<RelayWsUrlParts> ParseRelayWsStartUrl(const std::string& url) {
    RelayWsUrlParts result;
    std::size_t pos = 0;

    const std::size_t scheme_end = url.find("://", pos);
    if (scheme_end == std::string::npos) {
        return std::nullopt;
    }
    result.scheme = url.substr(0, scheme_end);
    pos = scheme_end + 3;

    const std::size_t path_start = url.find('/', pos);
    std::string host_port;
    if (path_start == std::string::npos) {
        host_port = url.substr(pos);
        result.path = "/";
        result.query = "";
    } else {
        host_port = url.substr(pos, path_start - pos);
        const std::size_t query_start = url.find('?', path_start);
        if (query_start == std::string::npos) {
            result.path = url.substr(path_start);
            result.query = "";
        } else {
            result.path = url.substr(path_start, query_start - path_start);
            result.query = url.substr(query_start + 1);
        }
    }

    const std::size_t colon = host_port.find(':');
    if (colon == std::string::npos) {
        result.host = host_port;
        result.port = result.scheme == "https" || result.scheme == "wss" ? 443 : 80;
    } else {
        result.host = host_port.substr(0, colon);
        result.port = std::stoi(host_port.substr(colon + 1));
    }

    return result;
}

namespace {

std::string ExtractQuotedString(const std::string& response, const std::string& key) {
    const std::size_t key_pos = response.find('"' + key + '"');
    if (key_pos == std::string::npos) {
        return "";
    }
    const std::size_t colon = response.find(':', key_pos);
    if (colon == std::string::npos) {
        return "";
    }
    const std::size_t quote_start = response.find('"', colon);
    if (quote_start == std::string::npos) {
        return "";
    }
    const std::size_t quote_end = response.find('"', quote_start + 1);
    if (quote_end == std::string::npos) {
        return "";
    }
    return response.substr(quote_start + 1, quote_end - quote_start - 1);
}

int ExtractInt(const std::string& response, const std::string& key, int default_value) {
    const std::size_t key_pos = response.find('"' + key + '"');
    if (key_pos == std::string::npos) {
        return default_value;
    }
    const std::size_t colon = response.find(':', key_pos);
    if (colon == std::string::npos) {
        return default_value;
    }
    const std::size_t value_start = response.find_first_not_of(" \t", colon + 1);
    if (value_start == std::string::npos) {
        return default_value;
    }
    try {
        const std::size_t len = response.find_first_of(",}", value_start) - value_start;
        return std::stoi(response.substr(value_start, len));
    } catch (...) {
        return default_value;
    }
}

}  // namespace

RelayWsStartResponse ParseRelayStartResponse(const std::string& response) {
    RelayWsStartResponse result;
    result.session = ExtractQuotedString(response, "session");
    result.bus_count = ExtractInt(response, "bus_count", 0);
    return result;
}

}  // namespace encos
