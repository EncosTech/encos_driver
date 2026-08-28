#include "relay/relay_frame.h"

#include <cstring>

namespace encos {

namespace {

constexpr uint8_t kMagic[4] = {'E', 'M', 'R', '1'};
constexpr std::size_t kHeaderSize = 8;
constexpr std::size_t kRecordSize = 18;
constexpr std::size_t kMaxRecordsPerFrame = 255;

void WriteU8(std::vector<uint8_t>& out, uint8_t value) {
    out.push_back(value);
}

void WriteU16Le(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void WriteU32Le(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void WriteI32Le(std::vector<uint8_t>& out, int32_t value) {
    WriteU32Le(out, static_cast<uint32_t>(value));
}

uint8_t ReadU8(const std::vector<uint8_t>& data, std::size_t& offset) {
    return data[offset++];
}

uint16_t ReadU16Le(const std::vector<uint8_t>& data, std::size_t& offset) {
    uint16_t value =
        static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
    offset += 2;
    return value;
}

uint32_t ReadU32Le(const std::vector<uint8_t>& data, std::size_t& offset) {
    uint32_t value = static_cast<uint32_t>(data[offset]) |
                     (static_cast<uint32_t>(data[offset + 1]) << 8) |
                     (static_cast<uint32_t>(data[offset + 2]) << 16) |
                     (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return value;
}

int32_t ReadI32Le(const std::vector<uint8_t>& data, std::size_t& offset) {
    return static_cast<int32_t>(ReadU32Le(data, offset));
}

}  // namespace

std::vector<uint8_t> EncodeRelayFrames(RelayFrameType type,
                                       const std::vector<MotorMessage>& records) {
    std::vector<uint8_t> result;
    for (std::size_t base = 0; base < records.size(); base += kMaxRecordsPerFrame) {
        const std::size_t count = std::min(kMaxRecordsPerFrame, records.size() - base);
        result.reserve(result.size() + kHeaderSize + count * kRecordSize);
        for (uint8_t byte : kMagic) {
            WriteU8(result, byte);
        }
        WriteU8(result, static_cast<uint8_t>(type));
        WriteU8(result, static_cast<uint8_t>(count));
        WriteU16Le(result, 0);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& msg = records[base + i];
            WriteI32Le(result, static_cast<int32_t>(msg.bus_idx));
            WriteU32Le(result, msg.data.id);
            WriteU8(result, msg.data.frame_flags);
            WriteU8(result, msg.data.len);
            for (std::size_t j = 0; j < 8; ++j) {
                WriteU8(result, msg.data.data[j]);
            }
        }
    }
    if (records.empty()) {
        result.reserve(kHeaderSize);
        for (uint8_t byte : kMagic) {
            WriteU8(result, byte);
        }
        WriteU8(result, static_cast<uint8_t>(type));
        WriteU8(result, 0);
        WriteU16Le(result, 0);
    }
    return result;
}

std::optional<std::vector<RelayFrame>> DecodeRelayFrames(const std::vector<uint8_t>& data) {
    std::vector<RelayFrame> frames;
    std::size_t offset = 0;
    while (offset < data.size()) {
        if (offset + kHeaderSize > data.size()) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            if (data[offset + i] != kMagic[i]) {
                return std::nullopt;
            }
        }
        offset += 4;
        const uint8_t type_byte = ReadU8(data, offset);
        if (type_byte != static_cast<uint8_t>(RelayFrameType::RelayToHelper) &&
            type_byte != static_cast<uint8_t>(RelayFrameType::HelperToRelay)) {
            return std::nullopt;
        }
        const uint8_t count = ReadU8(data, offset);
        /* reserved = */ ReadU16Le(data, offset);
        const std::size_t payload_size = static_cast<std::size_t>(count) * kRecordSize;
        if (offset + payload_size > data.size()) {
            return std::nullopt;
        }
        RelayFrame frame;
        frame.type = static_cast<RelayFrameType>(type_byte);
        frame.records.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            MotorMessage msg{};
            msg.bus_idx = static_cast<int>(ReadI32Le(data, offset));
            msg.data.id = ReadU32Le(data, offset);
            msg.data.frame_flags = ReadU8(data, offset);
            msg.data.len = ReadU8(data, offset);
            if (msg.data.len > 8) {
                return std::nullopt;
            }
            for (std::size_t j = 0; j < 8; ++j) {
                msg.data.data[j] = ReadU8(data, offset);
            }
            frame.records.push_back(msg);
        }
        frames.push_back(std::move(frame));
    }
    return frames;
}

}  // namespace encos
