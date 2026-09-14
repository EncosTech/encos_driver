#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
namespace encos::ethernet {
/** @brief 已校验的管理协议CAN错误快照。 */
struct CanError {
    uint32_t sequence = 0, uptime = 0, channel = 0, flags = 0, psr = 0, ecr = 0, count = 0,
             merged = 0, recovered = 0;
};
/** @brief 校验长度、版本、设备身份及通道；不接受EMR1数据包。 */
std::optional<CanError> DecodeCanError(const uint8_t* data, std::size_t size,
                                       const std::array<uint8_t, 16>& identity);
}  // namespace encos::ethernet
