#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace encos::protocol {

/** @brief Battery 四类状态帧的基础 CAN ID */
inline constexpr std::array<std::uint32_t, 4> kBatteryStatusBaseIds{0x3F4u, 0x2F4u, 0x0F4u, 0x1F4u};

/** @brief 生成指定 Battery 索引的四个状态路由 ID */
constexpr std::array<std::uint32_t, 4> BatteryStatusIds(std::uint16_t idx) noexcept {
    return {kBatteryStatusBaseIds[0] + idx, kBatteryStatusBaseIds[1] + idx,
            kBatteryStatusBaseIds[2] + idx, kBatteryStatusBaseIds[3] + idx};
}

inline constexpr std::uint32_t kImuPriority = 3u;
inline constexpr std::uint32_t kImuDefaultSourceAddress = 0x59u;
inline constexpr std::uint32_t kImuAccelerationPgn = 0xF02Du;
inline constexpr std::uint32_t kImuAngularVelocityPgn = 0xF02Au;
inline constexpr std::uint32_t kImuEulerAnglePgn = 0xF029u;
inline constexpr std::uint32_t kImuQuaternionPgn = 0xF030u;

/** @brief 生成指定 IMU 索引和 PGN 的扩展 CAN ID */
constexpr std::uint32_t ImuCanId(std::uint32_t pgn, std::uint16_t idx) noexcept {
    return (kImuPriority << 26u) | (pgn << 8u) | (kImuDefaultSourceAddress + idx);
}

/** @brief 生成指定 IMU 索引的四个状态路由 ID */
constexpr std::array<std::uint32_t, 4> ImuStatusIds(std::uint16_t idx) noexcept {
    return {ImuCanId(kImuAccelerationPgn, idx), ImuCanId(kImuAngularVelocityPgn, idx),
            ImuCanId(kImuEulerAnglePgn, idx), ImuCanId(kImuQuaternionPgn, idx)};
}

inline constexpr std::uint32_t kPmsBaseStateId = 0x18F0FFF2u;
inline constexpr std::uint32_t kPmsV48Current1To4Id = 0x18F1FFF2u;
inline constexpr std::uint32_t kPmsV48AndV19CurrentId = 0x18F2FFF2u;
inline constexpr std::uint32_t kPmsCommandId = 0x18F3FFF2u;
inline constexpr std::array<std::uint32_t, 3> kPmsStatusIds{kPmsBaseStateId, kPmsV48Current1To4Id,
                                                            kPmsV48AndV19CurrentId};

inline constexpr std::uint32_t kGloveEncoderBaseId = 0x8100u;
inline constexpr std::uint32_t kGloveCalibrationId = 0x8150u;

}  // namespace encos::protocol
