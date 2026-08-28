#include "imu/imu.h"

#include "encos/driver_manager.h"
#include "imu/imu_impl.h"

namespace encos {

namespace {

uint16_t ReadU16Le(const uint8_t* data) {
    return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                                 (static_cast<uint16_t>(data[1]) << 8));
}

uint32_t ExtractUnsignedIntel(const uint8_t* data, uint8_t start_bit, uint8_t bit_len) {
    uint32_t result = 0;
    for (uint8_t bit = 0; bit < bit_len; ++bit) {
        const uint8_t absolute_bit = static_cast<uint8_t>(start_bit + bit);
        const bool enabled = (data[absolute_bit / 8u] & (1u << (absolute_bit % 8u))) != 0;
        if (enabled) {
            result |= (1u << bit);
        }
    }
    return result;
}

float Scale(uint32_t raw, float resolution, float offset) {
    return static_cast<float>(raw) * resolution + offset;
}

}  // namespace

bool Imu::Impl::OnMessage(const MotorPackMsg& message) {
    const auto route_ids = protocol::ImuStatusIds(idx);
    std::optional<ImuAcceleration> new_acceleration;
    std::optional<ImuAngularVelocity> new_angular_velocity;
    std::optional<ImuEulerAngle> new_euler_angle;
    std::optional<ImuQuaternion> new_quaternion;

    const auto id = static_cast<int>(message.id);
    if (id == static_cast<int>(route_ids[0]) && message.len >= 6) {
        ImuAcceleration parsed{};
        parsed.x = Scale(ReadU16Le(message.data), 0.01f, -320.0f);
        parsed.y = Scale(ReadU16Le(message.data + 2), 0.01f, -320.0f);
        parsed.z = Scale(ReadU16Le(message.data + 4), 0.01f, -320.0f);
        new_acceleration = parsed;
    } else if (id == static_cast<int>(route_ids[1]) && message.len >= 8) {
        ImuAngularVelocity parsed{};
        parsed.x = Scale(ExtractUnsignedIntel(message.data, 0, 20), 0.0078125f, -4000.0f);
        parsed.y = Scale(ExtractUnsignedIntel(message.data, 20, 20), 0.0078125f, -4000.0f);
        parsed.z = Scale(ExtractUnsignedIntel(message.data, 40, 20), 0.0078125f, -4000.0f);
        new_angular_velocity = parsed;
    } else if (id == static_cast<int>(route_ids[2]) && message.len >= 6) {
        ImuEulerAngle parsed{};
        parsed.pitch = Scale(ReadU16Le(message.data), 0.0078125f, -250.0f);
        parsed.roll = Scale(ReadU16Le(message.data + 2), 0.0078125f, -250.0f);
        parsed.heading = Scale(ReadU16Le(message.data + 4), 0.0078125f, -250.0f);
        new_euler_angle = parsed;
    } else if (id == static_cast<int>(route_ids[3]) && message.len >= 8) {
        ImuQuaternion parsed{};
        parsed.qw = Scale(ReadU16Le(message.data), 0.000030519f, -1.0f);
        parsed.qx = Scale(ReadU16Le(message.data + 2), 0.000030519f, -1.0f);
        parsed.qy = Scale(ReadU16Le(message.data + 4), 0.000030519f, -1.0f);
        parsed.qz = Scale(ReadU16Le(message.data + 6), 0.000030519f, -1.0f);
        new_quaternion = parsed;
    }

    ImuStatus snapshot{};
    std::function<void(const ImuStatus&)> callback;
    bool status_changed = false;
    const auto now = std::chrono::steady_clock::now();
    {
        platform::LockGuard<platform::Mutex> lock(status_mutex);
        if (new_acceleration.has_value()) {
            current_status.acceleration = *new_acceleration;
            last_acceleration_update = now;
            status_changed = true;
        }
        if (new_angular_velocity.has_value()) {
            current_status.angular_velocity = *new_angular_velocity;
            last_angular_velocity_update = now;
            status_changed = true;
        }
        if (new_euler_angle.has_value()) {
            current_status.euler_angle = *new_euler_angle;
            last_euler_angle_update = now;
            status_changed = true;
        }
        if (new_quaternion.has_value()) {
            current_status.quaternion = *new_quaternion;
            last_quaternion_update = now;
            status_changed = true;
        }
        if (status_changed) {
            snapshot = current_status;
            callback = on_status;
        }
    }

    if (status_changed && callback) {
        callback(snapshot);
    }
    return status_changed;
}

ImuStatus Imu::Impl::GetStatusSnapshot() {
    platform::LockGuard<platform::Mutex> lock(status_mutex);
    const auto now = std::chrono::steady_clock::now();
    if (now - last_acceleration_update > state_timeout) {
        current_status.acceleration.reset();
    }
    if (now - last_angular_velocity_update > state_timeout) {
        current_status.angular_velocity.reset();
    }
    if (now - last_euler_angle_update > state_timeout) {
        current_status.euler_angle.reset();
    }
    if (now - last_quaternion_update > state_timeout) {
        current_status.quaternion.reset();
    }
    return current_status;
}

Imu::Imu(Bus* bus, uint16_t imu_idx, LoggerPtr logger,
         std::function<void(const MotorPackMsg&)> writer) {
    impl_ = std::make_unique<Impl>();
    impl_->idx = imu_idx;
    impl_->bus = bus;
    impl_->writer = std::move(writer);
    impl_->logger_ = std::move(logger);
}

Imu::~Imu() = default;

ImuStatus Imu::GetStatus() {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    return impl_->GetStatusSnapshot();
}

void Imu::OnMessage(const MotorPackMsg& message) {
    (void) impl_->OnMessage(message);
}

void Imu::SetOnStatus(std::function<void(const ImuStatus&)> callback) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    platform::LockGuard<platform::Mutex> lock(impl_->status_mutex);
    impl_->on_status = std::move(callback);
}

}  // namespace encos
