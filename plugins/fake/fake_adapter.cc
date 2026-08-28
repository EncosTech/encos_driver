#include "plugins/fake/fake_adapter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "motor/math_utils.h"
#include "plugins/fake/fake_protocol.h"

namespace encos {

FakeAdapter::~FakeAdapter() {}

namespace {

constexpr std::int64_t kMotorKeyStride = std::int64_t{1} << 32;

int DecodeBusIndex(std::int64_t key) {
    auto bus_idx = key / kMotorKeyStride;
    if (key % kMotorKeyStride < 0) {
        --bus_idx;
    }
    return static_cast<int>(bus_idx);
}

std::vector<uint8_t> EncodeFloatBe(float value) {
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(value));
    return {static_cast<uint8_t>(raw >> 24), static_cast<uint8_t>(raw >> 16),
            static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw)};
}

std::vector<uint8_t> EncodeU16(uint16_t value) {
    return {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
}

std::vector<uint8_t> EncodeI16(int16_t value) {
    return {static_cast<uint8_t>(static_cast<uint16_t>(value) >> 8),
            static_cast<uint8_t>(static_cast<uint16_t>(value) & 0xFF)};
}

}  // namespace

FakeAdapter::FakeAdapter(const std::string& interface_name, const std::string& logger_name,
                         encos::LogLevel log_level)
    : BaseAdapter(interface_name, logger_name, log_level), random_engine_(std::random_device{}()) {}

std::unordered_map<int, Bus*> FakeAdapter::GetBuses() {
    auto buses = GetKnownBusesSnapshot();
    std::vector<int> seeded_bus_indices;
    {
        const platform::LockGuard<platform::Mutex> lock(state_mutex_);
        seeded_bus_indices.reserve(snapshots_.size());
        for (const auto& [key, _] : snapshots_) {
            seeded_bus_indices.push_back(DecodeBusIndex(key));
        }
    }
    for (const int bus_idx : seeded_bus_indices) {
        if (buses.find(bus_idx) == buses.end()) {
            buses[bus_idx] = GetBus(bus_idx);
        }
    }
    return buses;
}

bool FakeAdapter::Ok() {
    return true;
}

FakeAdapterControl* FakeAdapter::GetFakeAdapterControl() {
    return this;
}

FakeMotorSnapshot FakeAdapter::CreateSnapshot(MotorModel model) const {
    FakeMotorSnapshot snapshot;
    snapshot.model = model;
    snapshot.position_rad = 0.0f;
    snapshot.ranges = GetMotorModelRanges(model);
    snapshot.kt = snapshot.ranges.kt;
    return snapshot;
}

void FakeAdapter::SeedMotor(int bus_idx, int motor_idx, MotorModel model,
                            const FakeSeedOptions& overrides) {
    auto snapshot = CreateSnapshot(model);
    if (overrides.reply_frame_flags.has_value()) {
        snapshot.reply_frame_flags = *overrides.reply_frame_flags;
    }
    SeedMotor(bus_idx, motor_idx, snapshot);
}

void FakeAdapter::SeedMotor(int bus_idx, int motor_idx, const FakeMotorSnapshot& snapshot) {
    (void) GetBus(bus_idx);
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    const auto key = MakeKey(bus_idx, motor_idx);
    snapshots_[key] = snapshot;
    FakeMotionState motion;
    motion.initialized = true;
    motion.current_speed = snapshot.speed_rad_s;
    motion.target_speed = snapshot.speed_rad_s;
    motion.acceleration_scale = 1.0f;
    motion.last_update = std::chrono::steady_clock::now();
    motions_[key] = motion;
}

FakeMotorSnapshot FakeAdapter::GetMotorSnapshot(int bus_idx, int motor_idx) {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    AdvanceMotionToNowLocked(bus_idx, motor_idx);
    auto it = snapshots_.find(MakeKey(bus_idx, motor_idx));
    if (it == snapshots_.end()) {
        throw std::out_of_range("Fake motor snapshot not found");
    }
    return it->second;
}

void FakeAdapter::SetReplyMode(FakeReplyMode mode) {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    reply_mode_ = mode;
}

void FakeAdapter::EnableAutoCreateMotor() {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    auto_create_motor_enabled_ = true;
}

void FakeAdapter::DisableAutoCreateMotor() {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    auto_create_motor_enabled_ = false;
}

void FakeAdapter::SetParameterWritePolicy(int bus_idx, int motor_idx, MotorParameter param,
                                          FakeWritePolicy policy) {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    parameter_policies_[MakeKey(bus_idx, motor_idx)][param] = policy;
}

void FakeAdapter::SetDecodedCommandObserver(DecodedCommandObserver observer) {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    decoded_command_observer_ = std::move(observer);
}

void FakeAdapter::ClearDecodedCommandObserver() {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    decoded_command_observer_ = nullptr;
}

void FakeAdapter::EnableCommandRecording(bool enabled) {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    command_recording_enabled_ = enabled;
}

void FakeAdapter::EnablePositionError(bool enabled) {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    position_error_enabled_ = enabled;
}

std::vector<FakeCommandRecord> FakeAdapter::GetCommandRecords() const {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    return std::vector<FakeCommandRecord>(command_records_.begin(), command_records_.end());
}

std::vector<std::string> FakeAdapter::GetFormattedSentCommands() const {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    std::vector<std::string> formatted;
    formatted.reserve(command_records_.size());
    for (const auto& record : command_records_) {
        formatted.push_back(record.formatted_text);
    }
    return formatted;
}

std::vector<FakeReplyRecord> FakeAdapter::GetReplyRecords() const {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    return reply_records_;
}

std::vector<MotorMessage> FakeAdapter::GetRawSentMessages() const {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    return raw_sent_messages_;
}

void FakeAdapter::ClearCommandRecords() {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    command_records_.clear();
    reply_records_.clear();
    raw_sent_messages_.clear();
}

void FakeAdapter::InjectMessage(const MotorMessage& message) {
    OnMessage({message});
}

MotorMessage FakeAdapter::MakeFeedbackMessage(int bus_idx, int motor_idx, const MotorStatus& status,
                                              int feedback_type) {
    const platform::LockGuard<platform::Mutex> lock(state_mutex_);
    return MakeFeedbackMessageLocked(bus_idx, motor_idx, status, feedback_type);
}

MotorMessage FakeAdapter::MakeFeedbackMessageLocked(int bus_idx, int motor_idx,
                                                    const MotorStatus& status,
                                                    int feedback_type) const {
    MotorPackMsg pack{};
    pack.id = static_cast<uint32_t>(motor_idx);
    pack.len = 8;
    pack.data[0] = static_cast<uint8_t>((feedback_type << 5) | static_cast<uint8_t>(status.error));

    const auto snapshot_it = snapshots_.find(MakeKey(bus_idx, motor_idx));
    const float current_range =
        snapshot_it == snapshots_.end() ? 0.0f : snapshot_it->second.ranges.current.max;
    if (feedback_type == 1) {
        const int pos_int = FloatToUint(status.position, -12.5f, 12.5f, 16);
        const int spd_int = FloatToUint(status.speed, -18.0f, 18.0f, 12);
        const int cur_int = FloatToUint(status.current, -current_range, current_range, 12);
        pack.data[1] = static_cast<uint8_t>(pos_int >> 8);
        pack.data[2] = static_cast<uint8_t>(pos_int & 0xFF);
        pack.data[3] = static_cast<uint8_t>(spd_int >> 4);
        pack.data[4] = static_cast<uint8_t>(((spd_int & 0x0F) << 4) | ((cur_int >> 8) & 0x0F));
        pack.data[5] = static_cast<uint8_t>(cur_int & 0xFF);
        pack.data[6] = static_cast<uint8_t>(status.motor_temperature * 2.0f + 50.0f);
        pack.data[7] = static_cast<uint8_t>(status.mos_temperature * 2.0f + 50.0f);
    } else if (feedback_type == 2) {
        const float position_deg = status.position * 180.0f / static_cast<float>(M_PI);
        const auto position_bytes = EncodeFloatBe(position_deg);
        const int16_t cur_int = static_cast<int16_t>(status.current * 100.0f);
        pack.data[1] = position_bytes[0];
        pack.data[2] = position_bytes[1];
        pack.data[3] = position_bytes[2];
        pack.data[4] = position_bytes[3];
        pack.data[5] = static_cast<uint8_t>(cur_int >> 8);
        pack.data[6] = static_cast<uint8_t>(cur_int & 0xFF);
        pack.data[7] = static_cast<uint8_t>(status.motor_temperature * 2.0f + 50.0f);
    } else {
        const float speed_rpm = status.speed * 30.0f / static_cast<float>(M_PI);
        const auto speed_bytes = EncodeFloatBe(speed_rpm);
        const int16_t cur_int = static_cast<int16_t>(status.current * 100.0f);
        pack.data[1] = speed_bytes[0];
        pack.data[2] = speed_bytes[1];
        pack.data[3] = speed_bytes[2];
        pack.data[4] = speed_bytes[3];
        pack.data[5] = static_cast<uint8_t>(cur_int >> 8);
        pack.data[6] = static_cast<uint8_t>(cur_int & 0xFF);
        pack.data[7] = static_cast<uint8_t>(status.motor_temperature * 2.0f + 50.0f);
    }
    return MotorMessage{bus_idx, pack};
}

MotorMessage FakeAdapter::MakeParameterReply(int bus_idx, int motor_idx, MotorParameter parameter,
                                             const std::vector<uint8_t>& payload,
                                             uint8_t frame_flags) const {
    MotorPackMsg pack{};
    pack.id = static_cast<uint32_t>(motor_idx);
    pack.frame_flags = frame_flags;
    pack.len = static_cast<uint8_t>(payload.size() + 2);
    pack.data[0] = static_cast<uint8_t>(0x05 << 5);
    pack.data[1] = static_cast<uint8_t>(parameter);
    std::memcpy(pack.data + 2, payload.data(), std::min<std::size_t>(payload.size(), 6));
    return MotorMessage{bus_idx, pack};
}

MotorMessage FakeAdapter::MakeWriteAck(int bus_idx, int motor_idx, MotorParameter parameter,
                                       const std::vector<uint8_t>& payload,
                                       uint8_t frame_flags) const {
    MotorPackMsg pack{};
    pack.id = static_cast<uint32_t>(motor_idx);
    pack.frame_flags = frame_flags;
    pack.len = static_cast<uint8_t>(payload.size() + 3);
    pack.data[0] = 0xFF;
    pack.data[1] = 0xFE;
    pack.data[2] = FakeRawIdFromWriteParameter(parameter);
    std::memcpy(pack.data + 3, payload.data(), std::min<std::size_t>(payload.size(), 5));
    return MotorMessage{bus_idx, pack};
}

MotorMessage MakeRawWriteAckMessage(int bus_idx, int motor_idx, uint8_t raw_parameter_id,
                                    const std::vector<uint8_t>& payload, uint8_t frame_flags) {
    MotorPackMsg pack{};
    pack.id = static_cast<uint32_t>(motor_idx);
    pack.frame_flags = frame_flags;
    pack.len = static_cast<uint8_t>(payload.size() + 3);
    pack.data[0] = 0xFF;
    pack.data[1] = 0xFE;
    pack.data[2] = raw_parameter_id;
    std::memcpy(pack.data + 3, payload.data(), std::min<std::size_t>(payload.size(), 5));
    return MotorMessage{bus_idx, pack};
}

void FakeAdapter::Send(const MotorMessage& message) {
    const platform::LockGuard<platform::RecursiveMutex> command_lock(command_mutex_);
    platform::UniqueLock<platform::Mutex> state_lock(state_mutex_);
    raw_sent_messages_.push_back(message);

    int logical_motor_id = static_cast<int>(message.data.id);
    if (message.data.id == 0x7FF && message.data.len >= 2) {
        logical_motor_id = (static_cast<int>(message.data.data[0]) << 8) | message.data.data[1];
    }

    const auto key = MakeKey(message.bus_idx, logical_motor_id);
    auto snapshot_it = snapshots_.find(key);

    // 在解码前先自动创建默认模型，确保首次命令的解码和参数回复使用同一组 EC_A4310_P2 范围
    if (snapshot_it == snapshots_.end() && auto_create_motor_enabled_) {
        state_lock.unlock();
        (void) GetBus(message.bus_idx);
        const auto new_snapshot = CreateSnapshot(MotorModel::EC_A4310_P2);
        state_lock.lock();
        snapshot_it = snapshots_.find(key);
        if (snapshot_it == snapshots_.end()) {
            snapshot_it = snapshots_.emplace(key, new_snapshot).first;
            FakeMotionState motion;
            motion.initialized = true;
            motion.current_speed = new_snapshot.speed_rad_s;
            motion.target_speed = new_snapshot.speed_rad_s;
            motion.acceleration_scale = 1.0f;
            motion.last_update = std::chrono::steady_clock::now();
            motions_[key] = motion;
        }
    }

    const FakeMotorSnapshot* snapshot =
        snapshot_it == snapshots_.end() ? nullptr : &snapshot_it->second;

    auto decoded = DecodeFakeCommand(message, snapshot);
    if (!decoded.has_value()) {
        return;
    }

    if (command_recording_enabled_) {
        command_records_.push_back(*decoded);
        if (command_records_.size() > kMaxCommandRecords) {
            command_records_.pop_front();
        }
    }

    const auto observer = decoded_command_observer_;
    bool observer_notified = false;
    auto notify_observer = [&]() {
        if (observer_notified) {
            return;
        }
        observer_notified = true;
        if (!observer) {
            return;
        }
        state_lock.unlock();
        try {
            observer(*decoded);
        } catch (...) {
            state_lock.lock();
            throw;
        }
        state_lock.lock();
    };

    auto dispatch_message = [&](const MotorMessage& response) {
        state_lock.unlock();
        try {
            OnMessage({response});
        } catch (...) {
            state_lock.lock();
            throw;
        }
        state_lock.lock();
    };

    if (snapshot_it == snapshots_.end()) {
        notify_observer();
        return;
    }

    if (reply_mode_ != FakeReplyMode::Automatic) {
        notify_observer();
        return;
    }

    auto emit_feedback = [&](int feedback_type) {
        if (feedback_type <= 0) {
            return;
        }
        MotorStatus status{};
        status.error = snapshot_it->second.error;
        status.position = snapshot_it->second.position_rad;
        status.speed = snapshot_it->second.speed_rad_s;
        status.current = snapshot_it->second.current_a;
        status.motor_temperature = snapshot_it->second.motor_temp_c;
        status.mos_temperature = snapshot_it->second.mos_temp_c;
        const auto response = MakeFeedbackMessageLocked(
            message.bus_idx, static_cast<int>(message.data.id), status, feedback_type);
        notify_observer();
        dispatch_message(response);
    };

    if (decoded->kind == FakeCommandKind::PVTControl) {
        const auto* payload = std::get_if<FakePVTControlPayload>(&decoded->payload);
        if (payload != nullptr) {
            snapshot_it->second.position_rad = payload->position;
            snapshot_it->second.speed_rad_s = payload->speed;
            snapshot_it->second.torque_nm = payload->torque;
            snapshot_it->second.current_a = payload->torque;
            auto& motion = motions_[key];
            motion.current_speed = payload->speed;
            motion.target_speed = payload->speed;
            motion.acceleration_scale = 1.0f;
            motion.last_update = std::chrono::steady_clock::now();
            notify_observer();
            emit_feedback(1);
        } else {
            notify_observer();
        }
        return;
    }

    if (decoded->kind == FakeCommandKind::PosControl) {
        const auto* payload = std::get_if<FakePosControlPayload>(&decoded->payload);
        if (payload != nullptr) {
            snapshot_it->second.position_rad = payload->position;
            snapshot_it->second.speed_rad_s = 0.0f;
            snapshot_it->second.current_a = payload->current;
            auto& motion = motions_[key];
            motion.current_speed = 0.0f;
            motion.target_speed = 0.0f;
            motion.acceleration_scale = 1.0f;
            motion.last_update = std::chrono::steady_clock::now();
            notify_observer();
            if (payload->feedback_type > 0) {
                MotorStatus status{};
                status.error = snapshot_it->second.error;
                status.position = payload->position;
                status.speed = payload->speed;
                status.current = payload->current;
                status.motor_temperature = snapshot_it->second.motor_temp_c;
                status.mos_temperature = snapshot_it->second.mos_temp_c;
                const auto response =
                    MakeFeedbackMessageLocked(message.bus_idx, static_cast<int>(message.data.id),
                                              status, payload->feedback_type);
                dispatch_message(response);
            }
        } else {
            notify_observer();
        }
        return;
    }

    if (decoded->kind == FakeCommandKind::SpdControl) {
        const auto* payload = std::get_if<FakeSpdControlPayload>(&decoded->payload);
        if (payload == nullptr) {
            notify_observer();
            return;
        }
        AdvanceMotionToNowLocked(message.bus_idx, logical_motor_id);
        snapshot_it->second.current_a = payload->current;
        auto& motion = motions_[key];
        motion.current_speed = payload->speed;
        motion.target_speed = payload->speed;
        motion.acceleration_scale = 1.0f;
        motion.last_update = std::chrono::steady_clock::now();
        snapshot_it->second.speed_rad_s = payload->speed;
        notify_observer();
        emit_feedback(payload->feedback_type);
        return;
    }

    if (decoded->kind == FakeCommandKind::CurControl) {
        const auto* payload = std::get_if<FakeCurControlPayload>(&decoded->payload);
        if (payload != nullptr) {
            AdvanceMotionToNowLocked(message.bus_idx, logical_motor_id);
            snapshot_it->second.current_a = payload->current;
            auto& motion = motions_[key];
            constexpr float kCurrentEpsilon = 1e-6f;
            if (std::abs(payload->current) < kCurrentEpsilon) {
                motion.target_speed = motion.current_speed;
                motion.acceleration_scale = 0.0f;
            } else {
                const float sign = payload->current > 0.0f ? 1.0f : -1.0f;
                motion.target_speed = sign * std::abs(snapshot_it->second.ranges.speed.max);
                motion.acceleration_scale = 1.0f;
            }
            motion.last_update = std::chrono::steady_clock::now();
            notify_observer();
            emit_feedback(payload->feedback_type);
        } else {
            notify_observer();
        }
        return;
    }

    if (decoded->kind == FakeCommandKind::TorControl) {
        const auto* payload = std::get_if<FakeTorControlPayload>(&decoded->payload);
        if (payload != nullptr) {
            AdvanceMotionToNowLocked(message.bus_idx, logical_motor_id);
            snapshot_it->second.torque_nm = payload->torque;
            snapshot_it->second.current_a = payload->torque;
            auto& motion = motions_[key];
            constexpr float kTorqueEpsilon = 1e-6f;
            if (std::abs(payload->torque) < kTorqueEpsilon) {
                motion.target_speed = motion.current_speed;
                motion.acceleration_scale = 0.0f;
            } else {
                const float sign = payload->torque > 0.0f ? 1.0f : -1.0f;
                motion.target_speed = sign * std::abs(snapshot_it->second.ranges.speed.max);
                motion.acceleration_scale = 1.0f;
            }
            motion.last_update = std::chrono::steady_clock::now();
            notify_observer();
            emit_feedback(payload->feedback_type);
        } else {
            notify_observer();
        }
        return;
    }

    if (decoded->kind == FakeCommandKind::Stop) {
        const auto* payload = std::get_if<FakeStopPayload>(&decoded->payload);
        if (payload != nullptr) {
            snapshot_it->second.speed_rad_s = 0.0f;
            snapshot_it->second.current_a = payload->current;
            auto& motion = motions_[key];
            motion.current_speed = 0.0f;
            motion.target_speed = 0.0f;
            motion.acceleration_scale = 1.0f;
            motion.last_update = std::chrono::steady_clock::now();
            notify_observer();
            emit_feedback(payload->feedback_type);
        } else {
            notify_observer();
        }
        return;
    }

    if (decoded->kind == FakeCommandKind::Brake) {
        const auto* payload = std::get_if<FakeBrakePayload>(&decoded->payload);
        if (payload != nullptr) {
            snapshot_it->second.brake_enabled = payload->enabled;
            MotorPackMsg ack{};
            ack.id = static_cast<uint32_t>(message.data.id);
            ack.len = 3;
            ack.data[0] = 0xB2;
            ack.data[1] = static_cast<uint8_t>(payload->enabled ? 1 : 0);
            notify_observer();
            dispatch_message(MotorMessage{message.bus_idx, ack});
        } else {
            notify_observer();
        }
        return;
    }

    if (decoded->kind == FakeCommandKind::SetPos) {
        const auto* payload = std::get_if<FakeSetPosPayload>(&decoded->payload);
        if (payload != nullptr) {
            snapshot_it->second.position_rad = payload->position_rad;
            MotorPackMsg ack{};
            ack.id = 0x7FF;
            ack.len = 4;
            ack.data[0] = message.data.data[0];
            ack.data[1] = message.data.data[1];
            ack.data[2] = 0x00;
            ack.data[3] = 0x03;
            notify_observer();
            dispatch_message(MotorMessage{message.bus_idx, ack});
        } else {
            notify_observer();
        }
        return;
    }

    if (decoded->kind == FakeCommandKind::ResetZeroPos) {
        snapshot_it->second.position_rad = 0.0f;
        MotorPackMsg ack{};
        ack.id = 0x7FF;
        ack.len = 4;
        ack.data[0] = message.data.data[0];
        ack.data[1] = message.data.data[1];
        ack.data[2] = 0x00;
        ack.data[3] = 0x03;
        notify_observer();
        dispatch_message(MotorMessage{message.bus_idx, ack});
        return;
    }

    if (decoded->kind == FakeCommandKind::SetId) {
        const auto* payload = std::get_if<FakeSetIdPayload>(&decoded->payload);
        if (payload != nullptr) {
            MotorPackMsg ack{};
            ack.id = 0x7FF;
            ack.len = 4;
            ack.data[0] = static_cast<uint8_t>(payload->target_id >> 8);
            ack.data[1] = static_cast<uint8_t>(payload->target_id & 0xFF);
            ack.data[2] = 0x00;
            ack.data[3] = 0x04;
            notify_observer();
            dispatch_message(MotorMessage{message.bus_idx, ack});
        } else {
            notify_observer();
        }
        return;
    }

    if (decoded->kind == FakeCommandKind::GetParameter) {
        const auto* payload = std::get_if<FakeGetParameterPayload>(&decoded->payload);
        if (payload == nullptr) {
            notify_observer();
            return;
        }
        std::vector<uint8_t> reply_payload;
        switch (payload->parameter) {
            case MotorParameter::Position: {
                AdvanceMotionToNowLocked(message.bus_idx, logical_motor_id);
                const float position_deg =
                    snapshot_it->second.position_rad * 180.0f / static_cast<float>(M_PI);
                reply_payload = EncodeFloatBe(position_deg);
                break;
            }
            case MotorParameter::Speed: {
                AdvanceMotionToNowLocked(message.bus_idx, logical_motor_id);
                const float speed_rpm =
                    snapshot_it->second.speed_rad_s * 30.0f / static_cast<float>(M_PI);
                reply_payload = EncodeFloatBe(speed_rpm);
                break;
            }
            case MotorParameter::Current: {
                AdvanceMotionToNowLocked(message.bus_idx, logical_motor_id);
                reply_payload = EncodeFloatBe(snapshot_it->second.current_a);
                break;
            }
            case MotorParameter::Acceleration:
                reply_payload = EncodeFloatBe(snapshot_it->second.acceleration);
                break;
            case MotorParameter::Kt:
                reply_payload = EncodeU16(static_cast<uint16_t>(snapshot_it->second.kt * 100.0f));
                break;
            case MotorParameter::PVTKpRange:
                reply_payload = EncodeU16(static_cast<uint16_t>(snapshot_it->second.ranges.kp.min));
                {
                    auto hi = EncodeU16(static_cast<uint16_t>(snapshot_it->second.ranges.kp.max));
                    reply_payload.insert(reply_payload.end(), hi.begin(), hi.end());
                }
                break;
            case MotorParameter::PVTKdRange:
                reply_payload = EncodeU16(static_cast<uint16_t>(snapshot_it->second.ranges.kd.min));
                {
                    auto hi = EncodeU16(static_cast<uint16_t>(snapshot_it->second.ranges.kd.max));
                    reply_payload.insert(reply_payload.end(), hi.begin(), hi.end());
                }
                break;
            case MotorParameter::PVTPosRange:
                reply_payload = EncodeI16(
                    static_cast<int16_t>(snapshot_it->second.ranges.position.min * 100.0f));
                {
                    auto hi = EncodeI16(
                        static_cast<int16_t>(snapshot_it->second.ranges.position.max * 100.0f));
                    reply_payload.insert(reply_payload.end(), hi.begin(), hi.end());
                }
                break;
            case MotorParameter::PVTSpdRange:
                reply_payload =
                    EncodeI16(static_cast<int16_t>(snapshot_it->second.ranges.speed.min * 100.0f));
                {
                    auto hi = EncodeI16(
                        static_cast<int16_t>(snapshot_it->second.ranges.speed.max * 100.0f));
                    reply_payload.insert(reply_payload.end(), hi.begin(), hi.end());
                }
                break;
            case MotorParameter::PVTTorRange:
                reply_payload =
                    EncodeI16(static_cast<int16_t>(snapshot_it->second.ranges.torque.min * 10.0f));
                {
                    auto hi = EncodeI16(
                        static_cast<int16_t>(snapshot_it->second.ranges.torque.max * 10.0f));
                    reply_payload.insert(reply_payload.end(), hi.begin(), hi.end());
                }
                break;
            case MotorParameter::PVTCurRange:
                reply_payload =
                    EncodeI16(static_cast<int16_t>(snapshot_it->second.ranges.current.min * 10.0f));
                {
                    auto hi = EncodeI16(
                        static_cast<int16_t>(snapshot_it->second.ranges.current.max * 10.0f));
                    reply_payload.insert(reply_payload.end(), hi.begin(), hi.end());
                }
                break;
            case MotorParameter::CanTimeout:
                reply_payload = EncodeU16(snapshot_it->second.can_timeout_ms);
                break;
            case MotorParameter::BrakeStatus:
                reply_payload = EncodeU16(snapshot_it->second.brake_enabled ? 1 : 0);
                break;
            default:
                reply_payload = EncodeFloatBe(0.0f);
                break;
        }
        const auto response = MakeParameterReply(message.bus_idx, static_cast<int>(message.data.id),
                                                 payload->parameter, reply_payload,
                                                 snapshot_it->second.reply_frame_flags);
        notify_observer();
        dispatch_message(response);
        return;
    }

    if (decoded->kind == FakeCommandKind::SetParameter) {
        const auto* payload = std::get_if<FakeSetParameterPayload>(&decoded->payload);
        if (payload == nullptr) {
            notify_observer();
            return;
        }

        FakeWritePolicy policy = FakeWritePolicy::Success;
        auto policy_it = parameter_policies_.find(key);
        if (policy_it != parameter_policies_.end()) {
            if (payload->parameter.has_value()) {
                auto it = policy_it->second.find(*payload->parameter);
                if (it != policy_it->second.end()) {
                    policy = it->second;
                }
            }
        }

        reply_records_.push_back(FakeReplyRecord{message.bus_idx, static_cast<int>(message.data.id),
                                                 decoded->kind, policy, true});

        if (policy != FakeWritePolicy::Success) {
            notify_observer();
            return;
        }

        if (payload->raw_parameter_id == 0x02 && !payload->raw_value.empty()) {
            snapshot_it->second.communication_mode =
                static_cast<MotorCommunicationMode>(payload->raw_value[0]);
        }

        if (payload->parameter == MotorParameter::CanTimeout && payload->raw_value.size() >= 2) {
            snapshot_it->second.can_timeout_ms =
                static_cast<uint16_t>((payload->raw_value[0] << 8) | payload->raw_value[1]);
        }
        if (payload->parameter == MotorParameter::Acceleration && payload->raw_value.size() >= 2) {
            const uint16_t raw =
                static_cast<uint16_t>((payload->raw_value[0] << 8) | payload->raw_value[1]);
            snapshot_it->second.acceleration = static_cast<float>(raw) / 100.0f;
            MotorPackMsg ack{};
            ack.id = static_cast<uint32_t>(message.data.id);
            ack.len = 3;
            ack.data[0] = static_cast<uint8_t>(0x04 << 5);
            ack.data[1] = 0x01;
            ack.data[2] = 1;
            notify_observer();
            dispatch_message(MotorMessage{message.bus_idx, ack});
            return;
        }
        if (payload->parameter == MotorParameter::PVTKpRange && payload->raw_value.size() >= 4) {
            snapshot_it->second.ranges.kp.min =
                static_cast<float>((payload->raw_value[0] << 8) | payload->raw_value[1]);
            snapshot_it->second.ranges.kp.max =
                static_cast<float>((payload->raw_value[2] << 8) | payload->raw_value[3]);
        }
        if (payload->parameter == MotorParameter::PVTKdRange && payload->raw_value.size() >= 4) {
            snapshot_it->second.ranges.kd.min =
                static_cast<float>((payload->raw_value[0] << 8) | payload->raw_value[1]);
            snapshot_it->second.ranges.kd.max =
                static_cast<float>((payload->raw_value[2] << 8) | payload->raw_value[3]);
        }
        if (payload->parameter == MotorParameter::PVTPosRange && payload->raw_value.size() >= 4) {
            snapshot_it->second.ranges.position.min =
                static_cast<float>(
                    static_cast<int16_t>((payload->raw_value[0] << 8) | payload->raw_value[1])) /
                100.0f;
            snapshot_it->second.ranges.position.max =
                static_cast<float>(
                    static_cast<int16_t>((payload->raw_value[2] << 8) | payload->raw_value[3])) /
                100.0f;
        }
        if (payload->parameter == MotorParameter::PVTSpdRange && payload->raw_value.size() >= 4) {
            snapshot_it->second.ranges.speed.min =
                static_cast<float>(
                    static_cast<int16_t>((payload->raw_value[0] << 8) | payload->raw_value[1])) /
                100.0f;
            snapshot_it->second.ranges.speed.max =
                static_cast<float>(
                    static_cast<int16_t>((payload->raw_value[2] << 8) | payload->raw_value[3])) /
                100.0f;
        }
        if (payload->parameter == MotorParameter::PVTTorRange && payload->raw_value.size() >= 4) {
            snapshot_it->second.ranges.torque.min =
                static_cast<float>(
                    static_cast<int16_t>((payload->raw_value[0] << 8) | payload->raw_value[1])) /
                10.0f;
            snapshot_it->second.ranges.torque.max =
                static_cast<float>(
                    static_cast<int16_t>((payload->raw_value[2] << 8) | payload->raw_value[3])) /
                10.0f;
        }
        if (payload->parameter == MotorParameter::PVTCurRange && payload->raw_value.size() >= 4) {
            snapshot_it->second.ranges.current.min =
                static_cast<float>(
                    static_cast<int16_t>((payload->raw_value[0] << 8) | payload->raw_value[1])) /
                10.0f;
            snapshot_it->second.ranges.current.max =
                static_cast<float>(
                    static_cast<int16_t>((payload->raw_value[2] << 8) | payload->raw_value[3])) /
                10.0f;
        }
        if (payload->parameter == MotorParameter::CurKpKi ||
            payload->parameter == MotorParameter::SpdKpKi ||
            payload->parameter == MotorParameter::PosKpKd ||
            payload->parameter == MotorParameter::Kt) {
            // Stored via raw ack only for now; snapshot fields not required by migrated tests.
        }

        auto ack = payload->parameter.has_value()
                       ? MakeWriteAck(message.bus_idx, static_cast<int>(message.data.id),
                                      *payload->parameter, payload->raw_value,
                                      snapshot_it->second.reply_frame_flags)
                       : MakeRawWriteAckMessage(message.bus_idx, static_cast<int>(message.data.id),
                                                payload->raw_parameter_id, payload->raw_value,
                                                snapshot_it->second.reply_frame_flags);
        notify_observer();
        dispatch_message(ack);
        return;
    }

    notify_observer();
}

void FakeAdapter::AdvanceMotionToNowLocked(int bus_idx, int motor_idx) {
    const auto key = MakeKey(bus_idx, motor_idx);
    auto snapshot_it = snapshots_.find(key);
    if (snapshot_it == snapshots_.end()) {
        return;
    }

    auto motion_it = motions_.find(key);
    const auto now = std::chrono::steady_clock::now();
    if (motion_it == motions_.end()) {
        FakeMotionState motion;
        motion.initialized = true;
        motion.current_speed = snapshot_it->second.speed_rad_s;
        motion.target_speed = snapshot_it->second.speed_rad_s;
        motion.acceleration_scale = 1.0f;
        motion.last_update = now;
        motions_[key] = motion;
        return;
    }

    const float elapsed = std::chrono::duration<float>(now - motion_it->second.last_update).count();
    motion_it->second.last_update = now;

    if (elapsed <= 0.0f) {
        snapshot_it->second.speed_rad_s = motion_it->second.current_speed;
        return;
    }

    const float start_speed = motion_it->second.current_speed;
    const float target_speed = motion_it->second.target_speed;
    const float accel =
        std::abs(snapshot_it->second.acceleration) * motion_it->second.acceleration_scale;

    float speed_delta = target_speed - start_speed;
    const float max_delta = accel * elapsed;
    if (speed_delta > max_delta) {
        speed_delta = max_delta;
    } else if (speed_delta < -max_delta) {
        speed_delta = -max_delta;
    }

    const float end_speed = start_speed + speed_delta;
    const float base_increment = (start_speed + end_speed) * 0.5f * elapsed;

    if (base_increment != 0.0f) {
        float increment = base_increment;
        if (position_error_enabled_) {
            const float error_bound = 0.1f * std::abs(base_increment);
            std::uniform_real_distribution<float> dist(-error_bound, error_bound);
            increment += dist(random_engine_);
        }
        snapshot_it->second.position_rad += increment;
    }

    motion_it->second.current_speed = end_speed;
    snapshot_it->second.speed_rad_s = end_speed;
}

std::int64_t FakeAdapter::MakeKey(int bus_idx, int motor_idx) const {
    return static_cast<std::int64_t>(bus_idx) * kMotorKeyStride +
           static_cast<std::uint32_t>(motor_idx);
}

BaseAdapter* CreateFakeAdapterStatic(const std::string& interface_name,
                                     const std::string& logger_name, encos::LogLevel log_level) {
    return new FakeAdapter(interface_name, logger_name, log_level);
}

}  // namespace encos
