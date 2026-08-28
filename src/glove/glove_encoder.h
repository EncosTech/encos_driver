#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "glove/glove.h"

namespace encos {

class EncosDriverManager;

/** @brief 单个手套编码器内部设备 */
class GloveEncoder {
    friend class EncosDriverManager;
    friend class Glove;

private:
    explicit GloveEncoder(uint8_t global_idx);

public:
    ~GloveEncoder();

private:
    /** @brief 路由分发入口：解码合法的 3 字节角度帧 */
    void OnMessage(const MotorPackMsg& message);
    /** @brief 在启用接收路由前连接状态上报目标 */
    void ConnectStatusCallback(std::function<void(std::size_t, GloveEncoderStatus)> callback);

    struct Impl {
        uint8_t global_idx = 0;
        std::function<void(std::size_t, GloveEncoderStatus)> on_status_callback;
    };
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos
