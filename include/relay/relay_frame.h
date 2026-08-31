#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "encos/export.h"
#include "motor/types.h"

namespace encos {

/**
 * @brief EMR1 帧类型
 */
enum class RelayFrameType : uint8_t {
    RelayToHelper = 1,  ///< 客户端发往 helper
    HelperToRelay = 2,  ///< helper 发往客户端
};

/**
 * @brief EMR1 帧结构
 */
struct RelayFrame {
    RelayFrameType type;                ///< 帧传输方向
    std::vector<MotorMessage> records;  ///< 帧内消息记录
};

/**
 * @brief 将一组 MotorMessage 编码为一个或多个 EMR1 帧
 * @param type 帧类型
 * @param records 电机消息记录，单帧最多 255 条，超过时自动拆帧
 * @return 编码后的字节序列（多帧直接拼接）
 */
ENCOS_BASE_API std::vector<uint8_t> EncodeRelayFrames(RelayFrameType type,
                                                      const std::vector<MotorMessage>& records);

/**
 * @brief 解码 EMR1 帧序列
 * @param data 原始字节序列
 * @return 解码后的帧列表；任意一帧校验失败则返回空
 */
ENCOS_BASE_API std::optional<std::vector<RelayFrame>> DecodeRelayFrames(
    const std::vector<uint8_t>& data);

}  // namespace encos
