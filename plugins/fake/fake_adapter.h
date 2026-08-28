#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "adapter/base_adapter.h"
#include "adapter/fake_adapter_control.h"
#include "platform/sync.h"

namespace encos {

constexpr float kDecodedFloatTolerance = 0.05f;
constexpr float kDecodedAngleTolerance = 0.01f;

struct FakeSeedOptions {
    std::optional<uint8_t> reply_frame_flags;
};

struct FakeReplyRecord {
    int bus_idx = 0;
    int motor_idx = 0;
    FakeCommandKind command_kind = FakeCommandKind::Unknown;
    FakeWritePolicy policy = FakeWritePolicy::Success;
    bool automatic = true;
};

class FakeAdapter : public BaseAdapter, public FakeAdapterControl {
public:
    explicit FakeAdapter(const std::string& interface_name,
                         const std::string& logger_name = "FakeAdapter",
                         LogLevel log_level = LogLevel::Info);
    ~FakeAdapter() override;

    std::unordered_map<int, Bus*> GetBuses() override;
    bool Ok() override;
    FakeAdapterControl* GetFakeAdapterControl() override;

    FakeMotorSnapshot CreateSnapshot(MotorModel model) const;
    void SeedMotor(int bus_idx, int motor_idx, MotorModel model,
                   const FakeSeedOptions& overrides = {});
    void SeedMotor(int bus_idx, int motor_idx, const FakeMotorSnapshot& snapshot);
    FakeMotorSnapshot GetMotorSnapshot(int bus_idx, int motor_idx) override;

    void SetReplyMode(FakeReplyMode mode) override;
    void EnableAutoCreateMotor() override;
    void DisableAutoCreateMotor() override;
    void SetParameterWritePolicy(int bus_idx, int motor_idx, MotorParameter param,
                                 FakeWritePolicy policy);

    void SetDecodedCommandObserver(FakeAdapterControl::DecodedCommandObserver observer) override;
    void ClearDecodedCommandObserver() override;
    void EnableCommandRecording(bool enabled = true) override;
    void EnablePositionError(bool enabled = true) override;

    std::vector<FakeCommandRecord> GetCommandRecords() const;
    std::vector<std::string> GetFormattedSentCommands() const;
    std::vector<FakeReplyRecord> GetReplyRecords() const;
    std::vector<MotorMessage> GetRawSentMessages() const;
    void ClearCommandRecords();

    void InjectMessage(const MotorMessage& message);
    MotorMessage MakeFeedbackMessage(int bus_idx, int motor_idx, const MotorStatus& status,
                                     int feedback_type = 1);
    MotorMessage MakeParameterReply(int bus_idx, int motor_idx, MotorParameter parameter,
                                    const std::vector<uint8_t>& payload,
                                    uint8_t frame_flags = 0) const;
    MotorMessage MakeWriteAck(int bus_idx, int motor_idx, MotorParameter parameter,
                              const std::vector<uint8_t>& payload, uint8_t frame_flags = 0) const;

protected:
    void Send(const MotorMessage& message) override;

private:
    std::int64_t MakeKey(int bus_idx, int motor_idx) const;
    void AdvanceMotionToNowLocked(int bus_idx, int motor_idx);
    MotorMessage MakeFeedbackMessageLocked(int bus_idx, int motor_idx, const MotorStatus& status,
                                           int feedback_type) const;

    struct FakeMotionState {
        bool initialized = false;
        std::chrono::steady_clock::time_point last_update;
        float current_speed = 0.0f;
        float target_speed = 0.0f;
        float acceleration_scale = 1.0f;
    };

    std::map<std::int64_t, FakeMotorSnapshot> snapshots_;
    std::map<std::int64_t, FakeMotionState> motions_;
    std::map<std::int64_t, std::map<MotorParameter, FakeWritePolicy>> parameter_policies_;
    FakeReplyMode reply_mode_ = FakeReplyMode::Automatic;
    bool auto_create_motor_enabled_ = false;
    bool command_recording_enabled_ = true;
    FakeAdapterControl::DecodedCommandObserver decoded_command_observer_;
    std::deque<FakeCommandRecord> command_records_;
    std::vector<FakeReplyRecord> reply_records_;
    std::vector<MotorMessage> raw_sent_messages_;
    std::mt19937 random_engine_;
    bool position_error_enabled_ = true;
    mutable platform::Mutex state_mutex_;
    platform::RecursiveMutex command_mutex_;

    static constexpr std::size_t kMaxCommandRecords = 100000;
};

/**
 * @brief 静态工厂函数，供生成的注册表包装器调用
 */
BaseAdapter* CreateFakeAdapterStatic(const std::string& interface_name,
                                     const std::string& logger_name = "FakeAdapter",
                                     LogLevel log_level = LogLevel::Info);

}  // namespace encos
