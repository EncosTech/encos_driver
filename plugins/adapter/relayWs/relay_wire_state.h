// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <stdexcept>

#include "adapter/adapter_extend.h"
#include "utils/emr2/relay_frame.h"

namespace encos {

/** @brief 连接级协议协商状态；调用方负责串行访问，并在切换能力前清空发送队列 */
struct RelayWsWireState {
    RelayProtocolMode mode = RelayProtocolMode::Auto;
    std::size_t configured_max_payload = 1448;
    RelayWireOptions options;
    std::uint32_t request_id = 0;
    bool requested = false;
    std::optional<RelayCapabilities> pending;

    explicit RelayWsWireState(const std::string& extend_json = "") {
        const auto extend = AdapterExtend::Parse(extend_json);
        if (!extend.DeclaredBuses().empty()) {
            throw std::invalid_argument("RelayWs protocol configuration is connection-wide");
        }
        for (const auto& [key, value] : extend.Common().items()) {
            if (key == "protocol" && value.is_string()) {
                mode = ParseRelayProtocolMode(value.get<std::string>());
            } else if (key == "max_payload" && value.is_number_integer() && value >= 128 &&
                       value <= 65535) {
                configured_max_payload = value.get<std::size_t>();
            } else {
                throw std::invalid_argument("Invalid RelayWs extend field: " + key);
            }
        }
        Reset();
    }

    /** @brief 每次连接前及关闭时重置，旧响应和旧命令不得跨连接复用 */
    void Reset() {
        ++request_id;
        requested = false;
        pending.reset();
        options = {};
        options.protocol = SelectRelayProtocol(mode, {});
        options.max_payload = configured_max_payload;
        options.session_id = request_id;
    }

    /** @brief 仅接受当前连接已发出请求的匹配响应 */
    void Receive(const RelayCapabilityMessage& message) {
        if (mode == RelayProtocolMode::Auto && requested && message.response &&
            message.request_id == request_id) {
            pending = message.capabilities;
        }
    }

    /** @brief 当前队列全部发送后采用协商结果 */
    void ApplyPending() {
        if (pending.has_value()) {
            options.protocol = SelectRelayProtocol(mode, *pending);
            options.max_payload =
                std::min(configured_max_payload, static_cast<std::size_t>(pending->max_payload));
            pending.reset();
        }
    }
};

}  // namespace encos
