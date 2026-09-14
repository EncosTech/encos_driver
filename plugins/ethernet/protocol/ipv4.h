#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "motor/types.h"

namespace encos::ethernet {
/** @brief Ethernet 链接配置，格式为网卡@IPv4[,UDP端口]。 */
struct Endpoint {
    std::string interface_name;
    std::string address;
    uint16_t port = 5000;
};
/** @brief IPv4/UDP 帧构造和过滤需要的两端地址，IPv4 为网络字节序。 */
struct Route {
    std::array<uint8_t, 6> local_mac{};
    std::array<uint8_t, 6> remote_mac{};
    uint32_t local_ip = 0;
    uint32_t remote_ip = 0;
    uint16_t local_port = 0;
    uint16_t remote_port = 5000;
};
/** @brief 严格解析四段十进制IPv4为网络顺序字节，拒绝前导零。 */
std::array<uint8_t, 4> ParseIPv4(const std::string& text);
/** @brief 将四个网络顺序字节格式化为IPv4文本。 */
std::string FormatIPv4(const uint8_t* bytes);
/** @brief 解析网卡@IPv4[,端口]，非法格式抛出异常。 */
Endpoint ParseEndpoint(const std::string& text);
/** @brief 构造带 IPv4/UDP 软件校验和的以太网帧。 */
std::vector<uint8_t> EncodePacket(const Route& route, const std::vector<uint8_t>& payload);
/** @brief 校验目的地址、长度和校验和并解码返回的 CAN 记录。 */
std::optional<MotorMessages> DecodePacket(const Route& route, const uint8_t* data,
                                          std::size_t size);
}  // namespace encos::ethernet
