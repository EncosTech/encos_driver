// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "utils/emr2/relay_frame.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace encos {
namespace {
constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kRecordHeaderSize = 12;
constexpr std::size_t kMaxPacketSize = 65535;

void Put16(std::vector<uint8_t>& out, std::size_t offset, uint16_t value) {
    out[offset] = static_cast<uint8_t>(value);
    out[offset + 1] = static_cast<uint8_t>(value >> 8);
}
void Put32(std::vector<uint8_t>& out, std::size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}
uint16_t Get16(const std::vector<uint8_t>& in, std::size_t offset) {
    return static_cast<uint16_t>(in[offset] | (static_cast<uint16_t>(in[offset + 1]) << 8));
}
uint32_t Get32(const std::vector<uint8_t>& in, std::size_t offset) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(in[offset + i]) << (8 * i);
    return value;
}
bool ValidType(RelayFrameType type) {
    return type == RelayFrameType::RelayToHelper || type == RelayFrameType::HelperToRelay;
}
bool ValidMessage(const MotorPackMsg& message, RelayProtocol protocol) {
    const auto flags = message.frame_flags;
    const bool fd = CanFrameFlagsUseCanFd(flags);
    return !(flags & ~kCanFrameFlagMask) && CanFrameFlagsHaveValidCanFdBits(flags) &&
           !(fd && CanFrameFlagsUseRtr(flags)) &&
           message.id <= (CanFrameFlagsUseExtendedId(flags) ? 0x1fffffffU : 0x7ffU) &&
           message.len <= (fd && protocol == RelayProtocol::Emr2 ? 64U : 8U);
}
bool ValidExtension(const RelayExtension& extension) {
    if (extension.type == 0 || (extension.type & 0x8000U) != 0 ||
        extension.value.size() > kMaxPacketSize - 4)
        return false;
    // 保留统一时间戳与关联标识布局；其余可选类型按长度透传。
    return (extension.type != 1 || extension.value.size() == 24) &&
           (extension.type != 2 || extension.value.size() == 12);
}
std::vector<uint8_t> EncodeExtensions(const std::vector<RelayExtension>& extensions) {
    std::vector<uint8_t> result;
    for (const auto& extension : extensions) {
        if (!ValidExtension(extension) ||
            extension.value.size() + 4 > kMaxPacketSize - result.size())
            throw std::invalid_argument("invalid or unsupported EMR2 extension");
        const auto offset = result.size();
        result.resize(offset + 4);
        Put16(result, offset, extension.type);
        Put16(result, offset + 2, static_cast<uint16_t>(extension.value.size()));
        result.insert(result.end(), extension.value.begin(), extension.value.end());
    }
    return result;
}
bool DecodeExtensions(const std::vector<uint8_t>& bytes, std::size_t start, std::size_t end,
                      std::vector<RelayExtension>& extensions) {
    while (start < end) {
        if (end - start < 4)
            return false;
        const auto type = Get16(bytes, start);
        const auto size = Get16(bytes, start + 2);
        start += 4;
        if (size > end - start)
            return false;
        RelayExtension extension{type, {bytes.begin() + start, bytes.begin() + start + size}};
        if (!ValidExtension(extension))
            return false;
        extensions.push_back(std::move(extension));
        start += size;
    }
    return true;
}
}  // namespace

RelayProtocolMode ParseRelayProtocolMode(const std::string& value) {
    if (value == "auto")
        return RelayProtocolMode::Auto;
    if (value == "emr1")
        return RelayProtocolMode::Emr1;
    if (value == "emr2")
        return RelayProtocolMode::Emr2;
    throw std::invalid_argument("protocol must be auto, emr1 or emr2");
}
RelayProtocol SelectRelayProtocol(RelayProtocolMode mode, const RelayCapabilities& capabilities) {
    return mode == RelayProtocolMode::Emr2 || (mode == RelayProtocolMode::Auto && capabilities.emr2)
               ? RelayProtocol::Emr2
               : RelayProtocol::Emr1;
}

std::vector<std::vector<uint8_t>> EncodeRelayPackets(RelayFrameType type,
                                                     const MotorMessages& messages,
                                                     RelayWireOptions& options) {
    std::vector<RelayRecord> records;
    records.reserve(messages.size());
    for (const auto& message : messages)
        records.push_back(RelayRecord{message, {}});
    return EncodeRelayPackets(type, records, {}, options);
}

std::vector<std::vector<uint8_t>> EncodeRelayPackets(RelayFrameType type,
                                                     const std::vector<RelayRecord>& records,
                                                     const std::vector<RelayExtension>& extensions,
                                                     RelayWireOptions& options) {
    if (!ValidType(type) || options.max_payload > kMaxPacketSize ||
        (options.protocol != RelayProtocol::Emr1 && options.protocol != RelayProtocol::Emr2))
        throw std::invalid_argument("invalid relay packet options");
    const bool emr2 = options.protocol == RelayProtocol::Emr2;
    const auto header_extensions = EncodeExtensions(extensions);
    if (!emr2 && !extensions.empty())
        throw std::invalid_argument("EMR1 cannot carry extensions");
    const auto header_size = emr2 ? kHeaderSize + header_extensions.size() : 8U;
    if (header_size > options.max_payload)
        throw std::invalid_argument("relay payload budget cannot fit header");
    std::vector<std::vector<uint8_t>> encoded_records;
    encoded_records.reserve(records.size());
    for (const auto& record : records) {
        if (!ValidMessage(record.message.data, options.protocol))
            throw std::invalid_argument(
                "CAN record cannot be represented by selected relay protocol");
        const auto record_extensions = EncodeExtensions(record.extensions);
        if (!emr2 && !record.extensions.empty())
            throw std::invalid_argument("EMR1 cannot carry record extensions");
        const auto& message = record.message;
        const auto size =
            emr2 ? kRecordHeaderSize + message.data.len + record_extensions.size() : 18U;
        if (size > options.max_payload - header_size)
            throw std::invalid_argument("single relay record exceeds payload budget");
        std::vector<uint8_t> encoded(size, 0);
        Put32(encoded, 0, static_cast<uint32_t>(message.bus_idx));
        Put32(encoded, 4, message.data.id);
        encoded[8] = message.data.frame_flags;
        encoded[9] = message.data.len;
        if (emr2) {
            Put16(encoded, 10, static_cast<uint16_t>(record_extensions.size()));
            std::copy_n(message.data.data, message.data.len, encoded.begin() + kRecordHeaderSize);
            std::copy(record_extensions.begin(), record_extensions.end(),
                      encoded.begin() + kRecordHeaderSize + message.data.len);
        } else {
            std::copy_n(message.data.data, 8, encoded.begin() + 10);
        }
        encoded_records.push_back(std::move(encoded));
    }
    std::vector<std::vector<uint8_t>> packets;
    auto sequence = options.sequence;
    std::size_t offset = 0;
    do {
        std::vector<uint8_t> packet(header_size, 0);
        std::memcpy(packet.data(), emr2 ? "EMR2" : "EMR1", 4);
        packet[4] = static_cast<uint8_t>(type);
        std::size_t count = 0;
        const std::size_t max_count = emr2 ? 65535U : 255U;
        while (offset < encoded_records.size() && count < max_count &&
               encoded_records[offset].size() <= options.max_payload - packet.size()) {
            const auto& record = encoded_records[offset++];
            packet.insert(packet.end(), record.begin(), record.end());
            ++count;
        }
        if (emr2) {
            Put16(packet, 6, static_cast<uint16_t>(header_size));
            Put16(packet, 8, static_cast<uint16_t>(packet.size()));
            Put16(packet, 10, static_cast<uint16_t>(count));
            Put32(packet, 12, options.session_id);
            Put32(packet, 16, sequence);
            Put32(packet, 20, options.batch_id);
            std::copy(header_extensions.begin(), header_extensions.end(),
                      packet.begin() + kHeaderSize);
        } else {
            packet[5] = static_cast<uint8_t>(count);
        }
        ++sequence;
        packets.push_back(std::move(packet));
    } while (offset < encoded_records.size());
    options.sequence = sequence;
    return packets;
}

std::optional<std::vector<RelayEnvelope>> DecodeRelayPackets(const std::vector<uint8_t>& bytes) {
    std::vector<RelayEnvelope> result;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 8)
            return std::nullopt;
        const bool emr2 = std::memcmp(bytes.data() + offset, "EMR2", 4) == 0;
        if (!emr2 && std::memcmp(bytes.data() + offset, "EMR1", 4) != 0)
            return std::nullopt;
        RelayEnvelope frame;
        frame.type = static_cast<RelayFrameType>(bytes[offset + 4]);
        if (!ValidType(frame.type))
            return std::nullopt;
        if (!emr2) {
            const auto length = 8U + static_cast<std::size_t>(bytes[offset + 5]) * 18U;
            if (length > bytes.size() - offset)
                return std::nullopt;
            const auto decoded =
                DecodeRelayFrames({bytes.begin() + offset, bytes.begin() + offset + length});
            if (!decoded || decoded->size() != 1)
                return std::nullopt;
            for (const auto& message : decoded->front().records)
                frame.records.push_back(RelayRecord{message, {}});
            offset += length;
        } else {
            if (bytes.size() - offset < kHeaderSize || bytes[offset + 5] != 0)
                return std::nullopt;
            const auto header_size = Get16(bytes, offset + 6);
            const auto length = Get16(bytes, offset + 8);
            const auto count = Get16(bytes, offset + 10);
            if (header_size < kHeaderSize || length < header_size ||
                length > bytes.size() - offset || Get32(bytes, offset + 24) != 0 ||
                Get32(bytes, offset + 28) != 0)
                return std::nullopt;
            frame.protocol = RelayProtocol::Emr2;
            frame.session_id = Get32(bytes, offset + 12);
            frame.sequence = Get32(bytes, offset + 16);
            frame.batch_id = Get32(bytes, offset + 20);
            if (!DecodeExtensions(bytes, offset + kHeaderSize, offset + header_size,
                                  frame.extensions))
                return std::nullopt;
            auto cursor = offset + header_size;
            const auto end = offset + length;
            if (count > (end - cursor) / kRecordHeaderSize)
                return std::nullopt;
            for (unsigned i = 0; i < count; ++i) {
                if (end - cursor < kRecordHeaderSize)
                    return std::nullopt;
                RelayRecord record;
                auto& message = record.message;
                const auto bus = Get32(bytes, cursor);
                // 避免超出 int32 范围时依赖实现定义的无符号到有符号转换。
                message.bus_idx = bus <= 0x7fffffffU ? static_cast<int>(bus)
                                                     : -1 - static_cast<int>(0xffffffffU - bus);
                message.data.id = Get32(bytes, cursor + 4);
                message.data.frame_flags = bytes[cursor + 8];
                message.data.len = bytes[cursor + 9];
                const auto extension_size = Get16(bytes, cursor + 10);
                cursor += kRecordHeaderSize;
                if (!ValidMessage(message.data, RelayProtocol::Emr2) ||
                    message.data.len > end - cursor)
                    return std::nullopt;
                std::copy_n(bytes.begin() + cursor, message.data.len, message.data.data);
                cursor += message.data.len;
                if (extension_size > end - cursor ||
                    !DecodeExtensions(bytes, cursor, cursor + extension_size, record.extensions))
                    return std::nullopt;
                cursor += extension_size;
                frame.records.push_back(std::move(record));
            }
            if (cursor != end)
                return std::nullopt;
            offset = end;
        }
        result.push_back(std::move(frame));
    }
    return result;
}

std::vector<uint8_t> EncodeRelayCapability(const RelayCapabilityMessage& message) {
    if (message.capabilities.max_payload < 44)
        throw std::invalid_argument("relay capability payload budget must be at least 44");
    std::vector<uint8_t> bytes(24, 0);
    std::memcpy(bytes.data(), "EMC2", 4);
    bytes[4] = 1;
    bytes[5] = message.response ? 2 : 1;
    Put16(bytes, 6, 24);
    Put32(bytes, 8, message.request_id);
    Put32(bytes, 12, message.capabilities.emr2 ? 3 : 1);
    Put16(bytes, 16, message.capabilities.max_payload);
    Put32(bytes, 20, message.capabilities.features);
    return bytes;
}
std::optional<RelayCapabilityMessage> DecodeRelayCapability(const std::vector<uint8_t>& bytes) {
    if (bytes.size() != 24 || std::memcmp(bytes.data(), "EMC2", 4) != 0 || bytes[4] != 1 ||
        (bytes[5] != 1 && bytes[5] != 2) || Get16(bytes, 6) != 24 || Get16(bytes, 18) != 0 ||
        (Get32(bytes, 12) != 1 && Get32(bytes, 12) != 3) || Get16(bytes, 16) < 44)
        return std::nullopt;
    return RelayCapabilityMessage{
        bytes[5] == 2,
        Get32(bytes, 8),
        {(Get32(bytes, 12) & 2U) != 0, Get16(bytes, 16), Get32(bytes, 20)}};
}
}  // namespace encos
