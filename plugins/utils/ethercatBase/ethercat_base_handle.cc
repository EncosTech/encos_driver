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
constexpr uint32_t kSystemCommandId = 0x7FF;

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

    if (message.data.id != kSystemCommandId) {
        for (std::size_t slot = start; slot < end; ++slot) {
            auto& target = packet.motor[slot];
            uint8_t cmd_id = static_cast<uint8_t>(target.data[0] >> 5);
            if (target.len != 0 && target.id == message.data.id && cmd_id <= 0x03) {
                target.frame_flags = SanitizeCanFrameFlags(message.data.frame_flags);
                target.len = message.data.len;
                std::memcpy(target.data, message.data.data, sizeof(message.data.data));
                return true;
            }
        }
    }

    for (std::size_t slot = start; slot < end; ++slot) {
        auto& target = packet.motor[slot];
        if (target.len == 0) {
            target.id = message.data.id;
            target.frame_flags = SanitizeCanFrameFlags(message.data.frame_flags);
            target.len = message.data.len;
            std::memcpy(target.data, message.data.data, sizeof(message.data.data));
            if constexpr (PacketTraits<PacketT>::kHasMotorNum) {
                packet.motor_num++;
            }
            return true;
        }
    }

    return false;
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

void ForwardMessages(const MotorPackMsg* motors, std::size_t motor_count, std::size_t slave,
                     std::size_t bus, MotorMessages& result) {
    for (std::size_t slot = 0; slot < motor_count; ++slot) {
        const auto& motor = motors[slot];
        if (motor.len == 0) {
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
    std::size_t output_pdo_size) {
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
    if (pending_batches_.empty() || pending_batches_.back().synchronized) {
        pending_batches_.push_back(PendingBatch{});
    }
    pending_batches_.back().messages.push_back(message);
}

void EthercatBaseHandle::QueueMessages(const MotorMessages& messages) {
    if (messages.empty()) {
        return;
    }

    ENCOS_TRACY_ZONE("EtherCAT::QueueMessages");
    platform::LockGuard<decltype(send_mutex_)> lock(send_mutex_);
    AppendPendingMessagesLocked(messages, false);
}

void EthercatBaseHandle::QueueSynchronizedMessages(const MotorMessages& messages) {
    if (messages.empty()) {
        return;
    }

    ENCOS_TRACY_ZONE("EtherCAT::QueueSynchronizedMessages");
    platform::LockGuard<decltype(send_mutex_)> lock(send_mutex_);
    AppendPendingMessagesLocked(messages, true);
}

std::vector<EthercatBaseHandle::OutputFrame> EthercatBaseHandle::PackMessages(
    const MotorMessages& messages, std::size_t slave_count) const {
    std::deque<QueuedFrame> queue;
    PackMessagesIntoQueue(messages, slave_count, queue, {});
    std::vector<OutputFrame> frames;
    frames.reserve(queue.size());
    for (auto& queued : queue) {
        frames.push_back(std::move(queued.frame));
    }
    return frames;
}

void EthercatBaseHandle::PackMessagesIntoQueue(
    const MotorMessages& messages, std::size_t slave_count, std::deque<QueuedFrame>& send_queue,
    const std::unordered_map<int, std::size_t>& bus_generations) const {
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

        const auto slave = static_cast<std::size_t>(slave_idx - 1);
        const auto generation = [&bus_generations, encoded]() {
            const auto found = bus_generations.find(encoded);
            return found == bus_generations.end() ? std::size_t{0} : found->second;
        }();
        const auto try_insert = [&config, slave, bus_idx, &msg](OutputFrame& frame) {
            switch (config.format) {
                case SlaveFormat::ClassicCan2Bus:
                    return TryInsertMessageIntoFrame<EthercatClassicCanMsg2>(
                        frame, slave, bus_idx, msg, config.message_size);
                case SlaveFormat::ClassicCan8Bus:
                    return TryInsertMessageIntoFrame<EthercatClassicCanMsg8>(
                        frame, slave, bus_idx, msg, config.message_size);
                case SlaveFormat::CanFd8Bus:
                    return TryInsertMessageIntoFrame<EthercatCanFdMsg8>(frame, slave, bus_idx, msg,
                                                                        config.message_size);
                case SlaveFormat::CanFd8Bus10Slots:
                    return TryInsertMessageIntoFrame<EthercatCanFdMsg8x10>(
                        frame, slave, bus_idx, msg, config.message_size);
                case SlaveFormat::CanFd3Bus:
                    return TryInsertMessageIntoFrame<EthercatCanFdMsg3>(frame, slave, bus_idx, msg,
                                                                        config.message_size);
                case SlaveFormat::Glove:
                    return TryInsertMessageIntoFrame<EthercatGloveSlots>(frame, slave, bus_idx, msg,
                                                                         config.message_size);
                case SlaveFormat::None:
                default:
                    return false;
            }
        };
        bool inserted = false;
        for (auto& queued : send_queue) {
            const auto found = queued.bus_generations.find(encoded);
            if (found != queued.bus_generations.end() && found->second != generation) {
                continue;
            }
            if (try_insert(queued.frame)) {
                queued.bus_generations.emplace(encoded, generation);
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
        queued.bus_generations.emplace(encoded, generation);
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
        buses.insert(encoded);
    }
    return buses;
}
MotorMessages EthercatBaseHandle::DecodeInputs(const std::vector<const uint8_t*>& inputs,
                                               std::size_t slave_count) {
    MotorMessages result;

    for (std::size_t slave = 0; slave < slave_count; ++slave) {
        if (slave >= slave_configs_.size() || slave >= inputs.size()) {
            continue;
        }

        const auto& config = slave_configs_[slave];
        const auto* input = inputs[slave];
        if (!input || config.bus_count == 0 || config.format == SlaveFormat::None) {
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
                DecodePacketInputs<EthercatCanFdMsg8>(input, bus_count, slave, result);
                break;
            case SlaveFormat::CanFd8Bus10Slots:
                DecodePacketInputs<EthercatCanFdMsg8x10>(input, bus_count, slave, result);
                break;
            case SlaveFormat::CanFd3Bus:
                DecodePacketInputs<EthercatCanFdMsg3>(input, bus_count, slave, result);
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

void EthercatBaseHandle::FlushPendingMessagesLocked(std::size_t slave_count) {
    if (pending_batches_.empty()) {
        return;
    }

    while (!pending_batches_.empty()) {
        auto batch = std::move(pending_batches_.front());
        pending_batches_.pop_front();
        PackBatchIntoSegmentsLocked(batch.messages, slave_count, batch.synchronized);
    }
    if (queued_frame_count_ > kSendQueueHighLoadThreshold) {
        const std::size_t discarded_frame_count = queued_frame_count_ - 1U;
        auto newest_frame = std::move(send_frames_.back());
        send_frames_.clear();
        send_frames_.push_back(std::move(newest_frame));
        queued_frame_count_ = 1U;
        logger_->warn("EtherCAT Send Queue high load: discarded {} pending EC frames",
                      discarded_frame_count);
    }
}

void EthercatBaseHandle::AppendPendingMessagesLocked(const MotorMessages& messages,
                                                     bool synchronized) {
    if (!synchronized && !pending_batches_.empty() && !pending_batches_.back().synchronized) {
        auto& pending = pending_batches_.back().messages;
        pending.insert(pending.end(), messages.begin(), messages.end());
        return;
    }

    pending_batches_.push_back(PendingBatch{messages, synchronized});
}

void EthercatBaseHandle::PackBatchIntoSegmentsLocked(const MotorMessages& messages,
                                                     std::size_t slave_count, bool synchronized) {
    if (messages.empty()) {
        return;
    }
    const auto packable_buses = CollectPackableBuses(messages, slave_count);
    if (synchronized) {
        for (const int bus : packable_buses) {
            ++bus_generations_[bus];
        }
    }
    const auto previous_size = send_frames_.size();
    PackMessagesIntoQueue(messages, slave_count, send_frames_, bus_generations_);
    queued_frame_count_ += send_frames_.size() - previous_size;
    if (synchronized) {
        for (const int bus : packable_buses) {
            ++bus_generations_[bus];
        }
    }
}

bool EthercatBaseHandle::PrepareNextFrame(OutputFrame& frame, std::size_t slave_count) {
    ENCOS_TRACY_ZONE("EtherCAT::TakePendingSendQueue");
    platform::LockGuard<decltype(send_mutex_)> lock(send_mutex_);
    FlushPendingMessagesLocked(slave_count);
    if (send_frames_.empty()) {
        frame.clear();
        return false;
    }

    frame = std::move(send_frames_.front().frame);
    send_frames_.pop_front();
    --queued_frame_count_;
    return true;
}
EthercatBaseHandle::ReceiveCallback EthercatBaseHandle::CopyReceiveCallback() const {
    platform::LockGuard<decltype(callback_mutex_)> lock(callback_mutex_);
    return receive_callback_;
}
