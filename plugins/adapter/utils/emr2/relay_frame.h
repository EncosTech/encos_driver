// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "utils/emr1/relay_frame.h"

namespace encos {

/** @brief 实际发送的协议版本 */
enum class RelayProtocol { Emr1, Emr2 };
/** @brief 自动协商或显式指定的发送模式；显式 Emr2 表示调用方已确认对端能力 */
enum class RelayProtocolMode { Auto, Emr1, Emr2 };
/** @brief 解析 auto/emr1/emr2；非法值抛出 invalid_argument */
ENCOS_BASE_API RelayProtocolMode ParseRelayProtocolMode(const std::string& value);

/** @brief 对端声明的能力；max_payload 是应用层单包上限，不含 UDP/IP 头 */
struct RelayCapabilities {
    bool emr2 = false;
    std::uint16_t max_payload = 1448;
    std::uint32_t features = 0;
};
/** @brief 根据配置和对端能力选择协议；Auto 未确认时保持 Emr1 */
ENCOS_BASE_API RelayProtocol SelectRelayProtocol(RelayProtocolMode mode,
                                                 const RelayCapabilities& capabilities);

/** @brief 可选 TLV；最高位表示必须理解，本版本拒绝所有此类扩展 */
struct RelayExtension {
    std::uint16_t type = 0;
    std::vector<std::uint8_t> value;
};
/** @brief 附带扩展元数据的单条 CAN 记录 */
struct RelayRecord {
    MotorMessage message{};
    std::vector<RelayExtension> extensions;
};
/** @brief 完整解码的独立包；保留未知可选扩展 */
struct RelayEnvelope {
    RelayProtocol protocol = RelayProtocol::Emr1;
    RelayFrameType type = RelayFrameType::RelayToHelper;
    std::uint32_t session_id = 0;
    std::uint32_t sequence = 0;
    std::uint32_t batch_id = 0;
    std::vector<RelayExtension> extensions;
    std::vector<RelayRecord> records;
};
/** @brief 编码参数；每个独立包递增 sequence；调用失败时不改变参数 */
struct RelayWireOptions {
    RelayProtocol protocol = RelayProtocol::Emr1;
    std::size_t max_payload = 1448;
    std::uint32_t session_id = 0;
    std::uint32_t sequence = 0;
    std::uint32_t batch_id = 0;
};
/** @brief 按完整记录和载荷预算拆包，整批校验失败抛异常；不做 IP 分片或接收重组 */
ENCOS_BASE_API std::vector<std::vector<std::uint8_t>> EncodeRelayPackets(
    RelayFrameType type, const MotorMessages& messages, RelayWireOptions& options);
/** @brief 编码带 TLV 的记录；EMR1 不允许丢弃扩展，单条记录装不下时抛异常 */
ENCOS_BASE_API std::vector<std::vector<std::uint8_t>> EncodeRelayPackets(
    RelayFrameType type, const std::vector<RelayRecord>& records,
    const std::vector<RelayExtension>& extensions, RelayWireOptions& options);
/** @brief 解码一个或连续多个 EMR1/EMR2 包；任一包非法则整体失败 */
ENCOS_BASE_API std::optional<std::vector<RelayEnvelope>> DecodeRelayPackets(
    const std::vector<std::uint8_t>& bytes);

/** @brief EMC2 能力消息，与数据报分离；用于有序连接协商，request_id 必须匹配当前连接 */
struct RelayCapabilityMessage {
    bool response = false;
    std::uint32_t request_id = 0;
    RelayCapabilities capabilities;
};
/** @brief 编码固定 24 字节 EMC2 请求/响应 */
ENCOS_BASE_API std::vector<std::uint8_t> EncodeRelayCapability(
    const RelayCapabilityMessage& message);
/** @brief 严格解码 EMC2 能力消息，失败返回空 */
ENCOS_BASE_API std::optional<RelayCapabilityMessage> DecodeRelayCapability(
    const std::vector<std::uint8_t>& bytes);

}  // namespace encos
