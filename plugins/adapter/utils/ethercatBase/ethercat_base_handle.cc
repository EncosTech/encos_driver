// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "ethercat_base_handle.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <type_traits>
#include <unordered_set>

namespace {
bool CheckedAdd(std::size_t value, std::size_t* total) {
    if (*total > std::numeric_limits<std::size_t>::max() - value) {
        return false;
    }
    *total += value;
    return true;
}

constexpr std::size_t kSendQueueHighLoadThreshold = 3;
constexpr std::size_t kSlotPayloadSize = sizeof(EthercatCanSlot::data);
constexpr uint8_t kFragmentFlag = 0x10;
constexpr unsigned kFragmentIndexShift = 5;

std::size_t SlotsForPayload(std::size_t length) {
    return (length + kSlotPayloadSize - 1) / kSlotPayloadSize;
}

bool IsValidCanFdPdoMessage(uint32_t id, uint8_t flags, unsigned length) {
    const bool fd = (flags & kCanFrameFlagFdBit1) != 0;
    const auto max_id = CanFrameFlagsUseExtendedId(flags) ? 0x1FFFFFFFU : 0x7FFU;
    return id != 0 && id <= max_id && length != 0 &&
           length <= (fd ? sizeof(MotorPackMsg::data) : kSlotPayloadSize) &&
           (fd ? !CanFrameFlagsUseRtr(flags) : (flags & kCanFrameFlagFdBit2) == 0);
}

bool IsValidCanFdPdoOutput(const MotorPackMsg& data, bool legacy_short_slots) {
    if (legacy_short_slots && data.len <= kSlotPayloadSize) {
        return data.len != 0;
    }
    return data.frame_flags == SanitizeCanFrameFlags(data.frame_flags) &&
           IsValidCanFdPdoMessage(data.id, data.frame_flags, data.len);
}

uint32_t ReadSlotId(const EthercatCanSlot& slot) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&slot);
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

void WriteSlotId(EthercatCanSlot& slot, uint32_t id) {
    auto* bytes = reinterpret_cast<uint8_t*>(&slot);
    for (unsigned i = 0; i < sizeof(id); ++i) {
        bytes[i] = static_cast<uint8_t>(id >> (8 * i));
    }
}

template <typename PacketT>
struct PacketTraits;

template <>
struct PacketTraits<EthercatClassicCanMsg2> {
    static constexpr std::size_t kBusCount = 2;
    static constexpr std::size_t kMotorsPerBus = 3;
    static constexpr bool kHasMotorNum = true;
};

template <>
struct PacketTraits<EthercatClassicCanMsg8> {
    static constexpr std::size_t kBusCount = 8;
    static constexpr std::size_t kMotorsPerBus = 3;
    static constexpr bool kHasMotorNum = true;
};

template <>
struct PacketTraits<EthercatCanFdMsg8> {
    static constexpr std::size_t kBusCount = 8;
    static constexpr std::size_t kMotorsPerBus = 8;
    static constexpr bool kHasMotorNum = false;
};

template <>
struct PacketTraits<EthercatCanFdMsg8x10> {
    static constexpr std::size_t kBusCount = 8;
    static constexpr std::size_t kMotorsPerBus = 10;
    static constexpr bool kHasMotorNum = false;
};

template <>
struct PacketTraits<EthercatCanFdMsg3> {
    static constexpr std::size_t kBusCount = 3;
    static constexpr std::size_t kMotorsPerBus = 8;
    static constexpr bool kHasMotorNum = false;
};

template <>
struct PacketTraits<EthercatGloveSlots> {
    static constexpr std::size_t kBusCount = 5;
    static constexpr std::size_t kMotorsPerBus = 10;
    static constexpr bool kHasMotorNum = false;
};

template <typename PacketT>
bool InsertMessageIntoPacket(PacketT& packet, uint16_t bus_idx, const MotorMessage& message) {
    if (bus_idx >= PacketTraits<PacketT>::kBusCount) {
        return false;
    }

    const std::size_t start =
        static_cast<std::size_t>(bus_idx) * PacketTraits<PacketT>::kMotorsPerBus;
    const std::size_t end = start + PacketTraits<PacketT>::kMotorsPerBus;

    for (std::size_t slot = start; slot < end; ++slot) {
        auto& target = packet.motor[slot];
        if (target.len == 0) {
            target.id = message.data.id;
            target.frame_flags = SanitizeCanFrameFlags(message.data.frame_flags);
            target.len = message.data.len;
            std::memcpy(target.data, message.data.data, sizeof(target.data));
            if constexpr (PacketTraits<PacketT>::kHasMotorNum) {
                packet.motor_num++;
            }
            return true;
        }
    }

    return false;
}

bool InsertCanFdMessage(EthercatOutputBuffer& buffer, std::size_t message_size, uint16_t bus_idx,
                        std::size_t slots_per_bus, const MotorPackMsg& data) {
    if (buffer.empty()) {
        buffer.reset(message_size);
    }
    auto* begin = reinterpret_cast<EthercatCanSlot*>(buffer.data()) + bus_idx * slots_per_bus;
    auto* end = begin + slots_per_bus;
    auto* target = std::find_if(begin, end, [](const auto& slot) {
        return slot.len == 0;
    });
    const auto count = SlotsForPayload(data.len);
    if (count > static_cast<std::size_t>(end - target)) {
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        auto& slot = target[i];
        WriteSlotId(slot, data.id);
        slot.len = data.len;
        slot.frame_flags = SanitizeCanFrameFlags(data.frame_flags);
        if (count > 1) {
            slot.frame_flags |= static_cast<uint8_t>(kFragmentFlag | (i << kFragmentIndexShift));
        }
        if (!CanFrameFlagsUseRtr(data.frame_flags)) {
            const auto offset = i * kSlotPayloadSize;
            std::memcpy(slot.data, data.data + offset,
                        std::min(kSlotPayloadSize, data.len - offset));
        }
    }
    return true;
}

template <typename PacketT>
bool TryInsertMessageIntoFrame(EthercatOutputFrame& frame, std::size_t slave, uint16_t bus_idx,
                               const MotorMessage& message, std::size_t message_size) {
    static_assert(std::is_trivially_copyable_v<PacketT>, "PacketT must be byte-addressable");
    static_assert(alignof(PacketT) == alignof(uint8_t),
                  "PacketT must be safe to access in a byte buffer");

    if (slave >= frame.size() || message_size != sizeof(PacketT)) {
        return false;
    }

    if (message_size > kEthercatMaxMessageSize) {
        return false;
    }

    auto& buffer = frame[slave];
    if (buffer.empty() || buffer.size() != message_size) {
        buffer.reset(message_size);
    }

    auto* packet = reinterpret_cast<PacketT*>(buffer.data());
    return InsertMessageIntoPacket(*packet, bus_idx, message);
}

void ForwardMessages(const EthercatCanSlot* motors, std::size_t motor_count, std::size_t slave,
                     std::size_t bus, MotorMessages& result) {
    for (std::size_t slot = 0; slot < motor_count; ++slot) {
        const auto& motor = motors[slot];
        if (motor.len == 0 || motor.len > sizeof(motor.data)) {
            continue;
        }
        MotorMessage message{};
        message.bus_idx =
            static_cast<int>((static_cast<int>(slave) << 16) | static_cast<uint16_t>(bus));
        message.data.id = motor.id;
        message.data.frame_flags = SanitizeCanFrameFlags(motor.frame_flags);
        message.data.len = motor.len;
        std::memcpy(message.data.data, motor.data, sizeof(motor.data));
        result.push_back(message);
    }
}

template <typename PacketT>
void DecodePacketInputs(const uint8_t* input, std::size_t bus_count, std::size_t slave,
                        MotorMessages& result) {
    const auto* src = reinterpret_cast<const PacketT*>(input);
    const auto buses = std::min<std::size_t>(bus_count, PacketTraits<PacketT>::kBusCount);
    for (std::size_t bus = 0; bus < buses; ++bus) {
        ForwardMessages(&src->motor[bus * PacketTraits<PacketT>::kMotorsPerBus],
                        PacketTraits<PacketT>::kMotorsPerBus, slave, bus, result);
    }
}
}  // namespace

std::optional<std::size_t> ComputeEthercatIoMapUpperBound(const ecx_contextt& context,
                                                          LoggerPtr logger) {
    if (context.slavecount <= 0 || context.slavecount >= EC_MAXSLAVE) {
        logger->error("Invalid EtherCAT slave count {}.", context.slavecount);
        return std::nullopt;
    }

    std::size_t upper_bound = 0;
    bool has_pdo = false;
    for (int slave = 1; slave <= context.slavecount; ++slave) {
        const auto& descriptor = context.slavelist[slave];
        bool has_outputs = false;
        bool has_inputs = false;

        for (int sm = 0; sm < EC_MAXSM; ++sm) {
            const auto type = descriptor.SMtype[sm];
            if (type > 4U) {
                logger->error("Unknown EtherCAT Sync Manager type {} at slave {}.",
                              static_cast<int>(type), slave);
                return std::nullopt;
            }
            if (type != 3U && type != 4U) {
                continue;
            }
            const auto length = static_cast<std::size_t>(descriptor.SM[sm].SMlength);
            if (length == 0) {
                logger->error("Zero-length PDO Sync Manager {} at slave {}.", sm, slave);
                return std::nullopt;
            }
            if (!CheckedAdd(length, &upper_bound)) {
                logger->error("EtherCAT Sync Manager size overflows at slave {}.", slave);
                return std::nullopt;
            }
            has_outputs = has_outputs || type == 3U;
            has_inputs = has_inputs || type == 4U;
            has_pdo = true;
        }
        const std::size_t alignment_bytes =
            static_cast<std::size_t>(has_outputs) + static_cast<std::size_t>(has_inputs);
        const std::size_t mailbox_status =
            (descriptor.mbx_l != 0U || descriptor.mbx_rl != 0U) ? 1U : 0U;
        if (!CheckedAdd(alignment_bytes, &upper_bound) ||
            !CheckedAdd(mailbox_status, &upper_bound)) {
            logger->error("EtherCAT I/O map allowance overflows at slave {}.", slave);
            return std::nullopt;
        }
        if (upper_bound > kEthercatMaxIoMapSize) {
            logger->error("EtherCAT I/O map upper bound {} exceeds {} bytes at slave {}.",
                          upper_bound, kEthercatMaxIoMapSize, slave);
            return std::nullopt;
        }
    }

    if (!has_pdo || upper_bound == 0) {
        logger->error("EtherCAT topology contains no usable PDO Sync Manager description.");
        return std::nullopt;
    }

    logger->debug("EtherCAT I/O map conservative upper bound: {} bytes.", upper_bound);
    return upper_bound;
}

bool IsEthercatMappedSizeValid(int mapped_bytes, std::size_t capacity) {
    return mapped_bytes > 0 && static_cast<std::size_t>(mapped_bytes) <= capacity;
}

EthercatBaseHandle::EthercatBaseHandle(LoggerPtr logger) : logger_(std::move(logger)) {}
void EthercatBaseHandle::SetReceiveCallback(ReceiveCallback callback) {
    platform::LockGuard<decltype(callback_mutex_)> lock(callback_mutex_);
    receive_callback_ = std::move(callback);
}

EthercatBaseHandle::SlaveConfig EthercatBaseHandle::ClassifyOutputPdoSize(
    std::size_t output_pdo_size, std::size_t input_pdo_size) {
    SlaveConfig config;
    config.message_size = output_pdo_size;
    if (output_pdo_size == sizeof(EthercatClassicCanMsg2)) {
        config.format = SlaveFormat::ClassicCan2Bus;
        config.bus_count = PacketTraits<EthercatClassicCanMsg2>::kBusCount;
        config.motors_per_bus = PacketTraits<EthercatClassicCanMsg2>::kMotorsPerBus;
    } else if (output_pdo_size == sizeof(EthercatClassicCanMsg8)) {
        config.format = SlaveFormat::ClassicCan8Bus;
        config.bus_count = PacketTraits<EthercatClassicCanMsg8>::kBusCount;
        config.motors_per_bus = PacketTraits<EthercatClassicCanMsg8>::kMotorsPerBus;
    } else if (output_pdo_size == sizeof(EthercatCanFdMsg8)) {
        config.format = SlaveFormat::CanFd8Bus;
        config.bus_count = PacketTraits<EthercatCanFdMsg8>::kBusCount;
        config.motors_per_bus = PacketTraits<EthercatCanFdMsg8>::kMotorsPerBus;
    } else if (output_pdo_size == sizeof(EthercatCanFdMsg8x10)) {
        config.format = SlaveFormat::CanFd8Bus10Slots;
        config.bus_count = PacketTraits<EthercatCanFdMsg8x10>::kBusCount;
        config.motors_per_bus = PacketTraits<EthercatCanFdMsg8x10>::kMotorsPerBus;
    } else if (output_pdo_size == sizeof(EthercatCanFdMsg3)) {
        config.format = SlaveFormat::CanFd3Bus;
        config.bus_count = PacketTraits<EthercatCanFdMsg3>::kBusCount;
        config.motors_per_bus = PacketTraits<EthercatCanFdMsg3>::kMotorsPerBus;
    } else if (output_pdo_size == sizeof(EthercatCanFdMsg3x10) &&
               input_pdo_size == sizeof(EthercatCanFdMsg3x10)) {
        config.format = SlaveFormat::CanFd3Bus10Slots;
        config.bus_count = EthercatCanFdMsg3x10::kBusCount;
        config.motors_per_bus = EthercatCanFdMsg3x10::kSlotsPerBus;
    } else if (output_pdo_size == sizeof(EthercatGloveSlots)) {
        config.format = SlaveFormat::Glove;
        config.bus_count = PacketTraits<EthercatGloveSlots>::kBusCount;
        config.motors_per_bus = PacketTraits<EthercatGloveSlots>::kMotorsPerBus;
    }
    return config;
}

bool EthercatBaseHandle::HasSupportedPdo(const SlaveConfig& config) {
    return config.format != SlaveFormat::None && config.bus_count != 0;
}

bool EthercatBaseHandle::UsesCanFdSlots(SlaveFormat format) {
    return format == SlaveFormat::CanFd3Bus || format == SlaveFormat::CanFd3Bus10Slots ||
           format == SlaveFormat::CanFd8Bus || format == SlaveFormat::CanFd8Bus10Slots;
}

std::vector<int> EthercatBaseHandle::GetBusSizes() const {
    std::vector<int> bus_sizes;
    for (const auto& config : slave_configs_) {
        bus_sizes.push_back(static_cast<int>(config.bus_count));
    }
    return bus_sizes;
}

void EthercatBaseHandle::QueueMessage(const MotorMessage& message) {
    ENCOS_TRACY_ZONE("EtherCAT::QueueMessage");
    platform::LockGuard<decltype(send_mutex_)> lock(send_mutex_);
    PackBatchIntoSegmentsLocked({message}, false);
}

void EthercatBaseHandle::QueueMessages(const MotorMessages& messages) {
    if (messages.empty()) {
        return;
    }

    ENCOS_TRACY_ZONE("EtherCAT::QueueMessages");
    platform::LockGuard<decltype(send_mutex_)> lock(send_mutex_);
    PackBatchIntoSegmentsLocked(messages, false);
}

void EthercatBaseHandle::QueueSynchronizedMessages(const MotorMessages& messages) {
    if (messages.empty()) {
        return;
    }

    ENCOS_TRACY_ZONE("EtherCAT::QueueSynchronizedMessages");
    platform::LockGuard<decltype(send_mutex_)> lock(send_mutex_);
    PackBatchIntoSegmentsLocked(messages, true);
}

std::vector<EthercatBaseHandle::OutputFrame> EthercatBaseHandle::PackMessages(
    const MotorMessages& messages, std::size_t slave_count) const {
    std::deque<QueuedFrame> queue;
    PackMessagesIntoQueue(messages, slave_count, queue);
    std::vector<OutputFrame> frames;
    frames.reserve(queue.size());
    for (auto& queued : queue) {
        frames.push_back(std::move(queued.frame));
    }
    return frames;
}

void EthercatBaseHandle::PackMessagesIntoQueue(
    const MotorMessages& messages, std::size_t slave_count, std::deque<QueuedFrame>& send_queue,
    const std::unordered_map<int, std::size_t>* bus_generations) const {
    if (slave_count == 0) {
        return;
    }

    for (const auto& msg : messages) {
        const int encoded = msg.bus_idx;
        const uint16_t slave_idx = static_cast<uint16_t>((encoded >> 16) + 1);
        const uint16_t bus_idx = static_cast<uint16_t>(encoded & 0xFFFF);
        if (slave_idx == 0 || static_cast<std::size_t>(slave_idx) > slave_count) {
            logger_->warn("Ignoring message for invalid slave index {}",
                          static_cast<int>(slave_idx));
            continue;
        }

        const auto& config = slave_configs_[static_cast<std::size_t>(slave_idx - 1)];
        if (config.bus_count == 0) {
            logger_->warn("Ignoring message for slave {} with unsupported format",
                          static_cast<int>(slave_idx));
            continue;
        }
        if (bus_idx >= config.bus_count) {
            logger_->warn("Ignoring message for slave {} bus {} (max {})",
                          static_cast<int>(slave_idx), static_cast<int>(bus_idx),
                          config.bus_count - 1);
            continue;
        }
        if (config.motors_per_bus == 0) {
            logger_->warn("Ignoring message for slave {} with zero slot capacity",
                          static_cast<int>(slave_idx));
            continue;
        }

        const bool can_fd = UsesCanFdSlots(config.format);
        const bool legacy_short_slots = config.format != SlaveFormat::CanFd3Bus10Slots;
        if (can_fd) {
            if (!IsValidCanFdPdoOutput(msg.data, legacy_short_slots)) {
                logger_->error(
                    "Invalid CAN frame for EtherCAT fragmented PDO: ID {}, flags {}, length {}",
                    msg.data.id, static_cast<unsigned>(msg.data.frame_flags),
                    static_cast<unsigned>(msg.data.len));
                continue;
            }
        } else if (msg.data.len > kSlotPayloadSize) {
            logger_->error("EtherCAT PDO payload exceeds 8 bytes: {}",
                           static_cast<unsigned>(msg.data.len));
            continue;
        }

        const auto slave = static_cast<std::size_t>(slave_idx - 1);
        const auto generation = [bus_generations, encoded]() {
            if (bus_generations == nullptr) {
                return std::size_t{0};
            }
            const auto found = bus_generations->find(encoded);
            return found == bus_generations->end() ? std::size_t{0} : found->second;
        }();
        const auto try_insert = [&config, slave, bus_idx, &msg, can_fd](OutputFrame& frame) {
            if (can_fd) {
                return InsertCanFdMessage(frame[slave], config.message_size, bus_idx,
                                          config.motors_per_bus, msg.data);
            }
            switch (config.format) {
                case SlaveFormat::ClassicCan2Bus:
                    return TryInsertMessageIntoFrame<EthercatClassicCanMsg2>(
                        frame, slave, bus_idx, msg, config.message_size);
                case SlaveFormat::ClassicCan8Bus:
                    return TryInsertMessageIntoFrame<EthercatClassicCanMsg8>(
                        frame, slave, bus_idx, msg, config.message_size);
                case SlaveFormat::Glove:
                    return TryInsertMessageIntoFrame<EthercatGloveSlots>(frame, slave, bus_idx, msg,
                                                                         config.message_size);
                case SlaveFormat::None:
                default:
                    return false;
            }
        };

        auto first = send_queue.begin();
        if (can_fd) {
            const auto last = std::find_if(
                send_queue.rbegin(), send_queue.rend(),
                [slave, bus_idx, &config](const auto& queued) {
                    const auto& buffer = queued.frame[slave];
                    if (buffer.empty()) {
                        return false;
                    }
                    const auto* slots = reinterpret_cast<const EthercatCanSlot*>(buffer.data());
                    return slots[bus_idx * config.motors_per_bus].len != 0;
                });
            if (last != send_queue.rend()) {
                first = std::prev(last.base());
            }
        }
        bool inserted = false;
        for (auto it = first; it != send_queue.end(); ++it) {
            auto& queued = *it;
            const auto found = queued.bus_generations.find(encoded);
            if (bus_generations != nullptr && found != queued.bus_generations.end() &&
                found->second != generation) {
                continue;
            }
            if (try_insert(queued.frame)) {
                if (bus_generations != nullptr) {
                    queued.bus_generations.emplace(encoded, generation);
                }
                inserted = true;
                break;
            }
        }

        if (inserted) {
            continue;
        }

        QueuedFrame queued;
        queued.frame.resize(slave_count);
        if (!try_insert(queued.frame)) {
            logger_->warn("Failed to Pack message for slave {} bus {}", static_cast<int>(slave_idx),
                          static_cast<int>(bus_idx));
            continue;
        }
        if (bus_generations != nullptr) {
            queued.bus_generations.emplace(encoded, generation);
        }
        send_queue.push_back(std::move(queued));
    }
}

std::unordered_set<int> EthercatBaseHandle::CollectPackableBuses(const MotorMessages& messages,
                                                                 std::size_t slave_count) const {
    std::unordered_set<int> buses;
    for (const auto& message : messages) {
        const int encoded = message.bus_idx;
        const uint16_t slave_idx = static_cast<uint16_t>((encoded >> 16) + 1);
        const uint16_t bus_idx = static_cast<uint16_t>(encoded & 0xFFFF);
        if (slave_idx == 0 || static_cast<std::size_t>(slave_idx) > slave_count) {
            continue;
        }
        const auto& config = slave_configs_[static_cast<std::size_t>(slave_idx - 1)];
        if (config.bus_count == 0 || bus_idx >= config.bus_count || config.motors_per_bus == 0) {
            continue;
        }
        if (UsesCanFdSlots(config.format)) {
            if (!IsValidCanFdPdoOutput(message.data,
                                       config.format != SlaveFormat::CanFd3Bus10Slots)) {
                continue;
            }
        } else if (message.data.len > kSlotPayloadSize) {
            continue;
        }
        buses.insert(encoded);
    }
    return buses;
}
MotorMessages EthercatBaseHandle::DecodeInputs(const std::vector<const uint8_t*>& inputs,
                                               std::size_t slave_count,
                                               std::chrono::microseconds cycle_period,
                                               InputClock::time_point now) {
    MotorMessages result;

    for (std::size_t slave = 0; slave < slave_count; ++slave) {
        if (slave >= slave_configs_.size()) {
            continue;
        }

        const auto& config = slave_configs_[slave];
        const auto* input = slave < inputs.size() ? inputs[slave] : nullptr;
        if (!input || config.bus_count == 0 || config.format == SlaveFormat::None) {
            if (slave < input_fragments_.size()) {
                for (auto& groups : input_fragments_[slave]) {
                    groups = {};
                }
            }
            continue;
        }

        const auto bus_count = config.bus_count;
        switch (config.format) {
            case SlaveFormat::ClassicCan2Bus:
                DecodePacketInputs<EthercatClassicCanMsg2>(input, bus_count, slave, result);
                break;
            case SlaveFormat::ClassicCan8Bus:
                DecodePacketInputs<EthercatClassicCanMsg8>(input, bus_count, slave, result);
                break;
            case SlaveFormat::CanFd8Bus:
            case SlaveFormat::CanFd8Bus10Slots:
            case SlaveFormat::CanFd3Bus:
            case SlaveFormat::CanFd3Bus10Slots:
                DecodeFragmentedInputs(input, slave, cycle_period, now, result);
                break;
            case SlaveFormat::Glove:
                DecodePacketInputs<EthercatGloveSlots>(input, bus_count, slave, result);
                break;
            case SlaveFormat::None:
            default:
                break;
        }
    }

    return result;
}

void EthercatBaseHandle::DecodeFragmentedInputs(const uint8_t* input, std::size_t slave,
                                                std::chrono::microseconds cycle_period,
                                                InputClock::time_point now, MotorMessages& result) {
    if (input_fragments_.size() != slave_configs_.size()) {
        input_fragments_.resize(slave_configs_.size());
    }
    const auto& config = slave_configs_[slave];
    auto& slave_fragments = input_fragments_[slave];
    if (slave_fragments.size() != config.bus_count) {
        slave_fragments.resize(config.bus_count);
    }
    const bool legacy_short_slots = config.format != SlaveFormat::CanFd3Bus10Slots;
    const auto* slots = reinterpret_cast<const EthercatCanSlot*>(input);
    for (std::size_t bus = 0; bus < config.bus_count; ++bus) {
        auto& groups = slave_fragments[bus];
        for (auto& group : groups) {
            if (group.from_previous_pdo || now >= group.deadline) {
                group.received = 0;
            }
            group.from_previous_pdo = group.received != 0;
        }

        const auto bus_idx = static_cast<int>((slave << 16) | bus);
        for (std::size_t i = 0; i < config.motors_per_bus; ++i) {
            const auto& slot = slots[bus * config.motors_per_bus + i];
            const auto flags = SanitizeCanFrameFlags(slot.frame_flags);
            const auto id = ReadSlotId(slot);
            if (slot.len == 0 || ((!legacy_short_slots || slot.len > kSlotPayloadSize) &&
                                  !IsValidCanFdPdoMessage(id, flags, slot.len))) {
                continue;
            }
            const auto normalized_flags = static_cast<uint8_t>(
                (flags & kCanFrameFlagFdBit1) != 0 ? flags | kCanFrameFlagFdMask : flags);
            if (slot.len <= kSlotPayloadSize) {
                if (slot.frame_flags == flags) {
                    MotorMessage message{bus_idx, {id, normalized_flags, slot.len, {}}};
                    if (!CanFrameFlagsUseRtr(flags)) {
                        std::memcpy(message.data.data, slot.data, slot.len);
                    }
                    result.push_back(message);
                }
                continue;
            }
            if ((slot.frame_flags & kFragmentFlag) == 0) {
                continue;
            }

            const auto count = SlotsForPayload(slot.len);
            const auto index = static_cast<unsigned>(slot.frame_flags >> kFragmentIndexShift);
            auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& entry) {
                return entry.received != 0 && entry.message.id == id &&
                       entry.message.frame_flags == flags && entry.message.len == slot.len;
            });
            if (index >= count) {
                if (group != groups.end()) {
                    group->received = 0;
                }
                continue;
            }
            if (index == 0) {
                if (group == groups.end()) {
                    group = std::find_if(groups.begin(), groups.end(), [](const auto& entry) {
                        return entry.received == 0;
                    });
                    if (group == groups.end()) {
                        continue;
                    }
                }
                *group = {{id, flags, slot.len, {}}, now + 2 * cycle_period, 0, false};
            } else if (group == groups.end()) {
                continue;
            }

            const auto offset = index * kSlotPayloadSize;
            const auto size = std::min(kSlotPayloadSize, slot.len - offset);
            auto* target = group->message.data + offset;
            const unsigned bit = 1U << index;
            if ((group->received & bit) != 0) {
                if (std::memcmp(target, slot.data, size) != 0) {
                    group->received = 0;
                }
                continue;
            }
            std::memcpy(target, slot.data, size);
            group->received |= bit;
            if (group->received == (1U << count) - 1) {
                group->message.frame_flags = normalized_flags;
                result.push_back({bus_idx, group->message});
                group->received = 0;
            }
        }
    }
}

void EthercatBaseHandle::ResetInputFragments() {
    for (auto& slave : input_fragments_) {
        for (auto& groups : slave) {
            groups = {};
        }
    }
}

void EthercatBaseHandle::TrimSendQueueLocked() {
    if (send_frames_.size() > kSendQueueHighLoadThreshold) {
        const std::size_t discarded_frame_count = send_frames_.size() - 1U;
        auto newest_frame = std::move(send_frames_.back());
        send_frames_.clear();
        send_frames_.push_back(std::move(newest_frame));
        if (!send_queue_warnings_suppressed_) {
            logger_->warn("EtherCAT Send Queue high load: discarded {} pending EC frames",
                          discarded_frame_count);
        }
    }
}

void EthercatBaseHandle::SetSendQueueWarningsSuppressed(bool suppressed) {
    send_queue_warnings_suppressed_ = suppressed;
}

void EthercatBaseHandle::PackBatchIntoSegmentsLocked(const MotorMessages& messages,
                                                     bool synchronized) {
    const auto slave_count = slave_configs_.size();
    PackMessagesIntoQueue(messages, slave_count, send_frames_,
                          synchronized ? &bus_generations_ : nullptr);
    if (synchronized) {
        for (const int bus : CollectPackableBuses(messages, slave_count)) {
            ++bus_generations_[bus];
        }
    }
    TrimSendQueueLocked();
}

bool EthercatBaseHandle::PrepareNextFrame(OutputFrame& frame) {
    ENCOS_TRACY_ZONE("EtherCAT::TakePendingSendQueue");
    platform::LockGuard<decltype(send_mutex_)> lock(send_mutex_);
    if (send_frames_.empty()) {
        frame.clear();
        return false;
    }
    frame = std::move(send_frames_.front().frame);
    send_frames_.pop_front();
    return true;
}
EthercatBaseHandle::ReceiveCallback EthercatBaseHandle::CopyReceiveCallback() const {
    platform::LockGuard<decltype(callback_mutex_)> lock(callback_mutex_);
    return receive_callback_;
}
