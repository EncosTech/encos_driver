#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>

#include "glove/glove.h"
#include "motor/types.h"
#include "platform/sync.h"

namespace encos {

class EncosDriverManager;

/** @brief 每个手指分区的内部校准命令和响应设备 */
class GloveCalibrator {
    friend class EncosDriverManager;
    friend class Glove;

private:
    explicit GloveCalibrator(std::function<void(const MotorPackMsg&)> writer);

public:
    ~GloveCalibrator();

    /** @brief 向本分区发送全部编码器校准命令 */
    GloveCalibrationStatus CalibrateAll();
    /** @brief 向本分区发送按掩码校准命令 */
    GloveCalibrationStatus CalibrateByMask(uint16_t encoder_mask);

private:
    GloveCalibrationStatus SendAndWait(const MotorPackMsg& message);

    /** @brief 路由分发入口：完成当前有效校准请求 */
    void OnMessage(const MotorPackMsg& message);

    struct Impl {
        static constexpr std::chrono::milliseconds kResponseTimeout{5};

        std::function<void(const MotorPackMsg&)> writer;
        platform::Mutex response_mutex;
        std::condition_variable_any response_condition;
        GloveCalibrationStatus response = GloveCalibrationStatus::Timeout;
        bool response_received = false;
        bool waiting_for_response = false;
    };
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos
