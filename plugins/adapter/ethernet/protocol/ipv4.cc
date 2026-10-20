// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "ipv4.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "gateway_payload.h"
namespace encos::ethernet {
namespace {
uint16_t Read16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
void Write16(uint8_t* p, uint16_t n) {
    p[0] = static_cast<uint8_t>(n >> 8);
    p[1] = static_cast<uint8_t>(n);
}
uint32_t Sum(const uint8_t* p, std::size_t n, uint32_t sum = 0) {
    while (n >= 2) {
        sum += Read16(p);
        p += 2;
        n -= 2;
    }
    if (n)
        sum += static_cast<uint32_t>(*p) << 8;
    return sum;
}
uint16_t Finish(uint32_t sum) {
    while (sum >> 16)
        sum = (sum & 0xffffU) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}
}  // namespace
std::array<uint8_t, 4> ParseIPv4(const std::string& text) {
    std::array<uint8_t, 4> result{};
    std::size_t begin = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const auto end = text.find('.', begin);
        const auto part = text.substr(begin, end == std::string::npos ? end : end - begin);
        if (part.empty() || part.size() > 3 ||
            part.find_first_not_of("0123456789") != std::string::npos ||
            (part.size() > 1 && part.front() == '0') || (i < 3) == (end == std::string::npos))
            throw std::invalid_argument("Invalid IPv4 address");
        const auto value = std::stoul(part);
        if (value > 255)
            throw std::invalid_argument("Invalid IPv4 address");
        result[i] = static_cast<uint8_t>(value);
        begin = end + 1;
    }
    return result;
}
std::string FormatIPv4(const uint8_t* bytes) {
    return std::to_string(bytes[0]) + "." + std::to_string(bytes[1]) + "." +
           std::to_string(bytes[2]) + "." + std::to_string(bytes[3]);
}
Endpoint ParseEndpoint(const std::string& text) {
    const auto at = text.find('@');
    if (at == std::string::npos || at == 0 || text.find('\0') != std::string::npos)
        throw std::invalid_argument("Ethernet expects interface@IPv4[,port]");
    Endpoint result;
    result.interface_name = text.substr(0, at);
    const auto comma = text.find(',', at + 1);
    result.address =
        text.substr(at + 1, comma == std::string::npos ? std::string::npos : comma - at - 1);
    const auto address = ParseIPv4(result.address);
    if ((address[0] & 0xf0) == 0xe0 || result.address == "0.0.0.0" ||
        result.address == "255.255.255.255" || result.address == "192.168.100.254")
        throw std::invalid_argument("Ethernet requires a unicast IPv4 address");
    if (comma != std::string::npos) {
        const auto port = text.substr(comma + 1);
        if (port.empty() || port.size() > 5 ||
            port.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("Invalid UDP port");
        const auto value = std::stoul(port);
        if (value == 0 || value > 65535)
            throw std::invalid_argument("Invalid UDP port");
        result.port = static_cast<uint16_t>(value);
    }
    return result;
}
std::vector<uint8_t> EncodePacket(const Route& route, const std::vector<uint8_t>& payload) {
    if (payload.size() > 1448)
        throw std::invalid_argument("UDP payload exceeds MTU budget");
    std::vector<uint8_t> packet(42 + payload.size(), 0);
    std::copy(route.remote_mac.begin(), route.remote_mac.end(), packet.begin());
    std::copy(route.local_mac.begin(), route.local_mac.end(), packet.begin() + 6);
    Write16(packet.data() + 12, 0x0800);
    auto* ip = packet.data() + 14;
    ip[0] = 0x45;
    ip[8] = 64;
    ip[9] = 17;
    Write16(ip + 2, static_cast<uint16_t>(28 + payload.size()));
    Write16(ip + 6, 0x4000);
    std::memcpy(ip + 12, &route.local_ip, 4);
    std::memcpy(ip + 16, &route.remote_ip, 4);
    Write16(ip + 10, Finish(Sum(ip, 20)));
    auto* udp = ip + 20;
    Write16(udp, route.local_port);
    Write16(udp + 2, route.remote_port);
    const auto length = static_cast<uint16_t>(8 + payload.size());
    Write16(udp + 4, length);
    std::copy(payload.begin(), payload.end(), packet.begin() + 42);
    const auto checksum = Finish(Sum(udp, length, Sum(ip + 12, 8) + 17 + length));
    Write16(udp + 6, checksum ? checksum : 0xffffU);
    return packet;
}
std::optional<MotorMessages> DecodePacket(const Route& route, const uint8_t* data,
                                          std::size_t size) {
    if (!data || size < 42 || !std::equal(route.local_mac.begin(), route.local_mac.end(), data) ||
        !std::equal(route.remote_mac.begin(), route.remote_mac.end(), data + 6) ||
        Read16(data + 12) != 0x0800)
        return std::nullopt;
    const auto* ip = data + 14;
    const std::size_t header = (ip[0] & 15) * 4;
    if ((ip[0] >> 4) != 4 || header < 20 || size < 14 + header + 8 || ip[9] != 17 ||
        (Read16(ip + 6) & 0x3fff) || Finish(Sum(ip, header)) != 0 ||
        std::memcmp(ip + 12, &route.remote_ip, 4) || std::memcmp(ip + 16, &route.local_ip, 4))
        return std::nullopt;
    const auto total = Read16(ip + 2);
    if (total < header + 8 || size < 14U + total)
        return std::nullopt;
    const auto* udp = ip + header;
    const auto length = Read16(udp + 4);
    if (length < 8 || length != total - header || Read16(udp) != route.remote_port ||
        Read16(udp + 2) != route.local_port)
        return std::nullopt;
    if (Read16(udp + 6) != 0 && Finish(Sum(udp, length, Sum(ip + 12, 8) + 17 + length)) != 0)
        return std::nullopt;
    return DecodePayload(udp + 8, length - 8);
}
}  // namespace encos::ethernet
