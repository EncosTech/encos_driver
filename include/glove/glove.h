#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "encos/export.h"
#include "motor/types.h"

namespace encos {

class Bus;
class EncosDriverManager;

/** @brief 单个手套编码器角度，单位为弧度，范围为 [0, 2pi]；nullopt 表示无有效数据 */
using GloveEncoderStatus = std::optional<float>;

/** @brief 单只手套的 5 路、每路 10 个编码器状态 */
using GloveStatus = std::array<std::array<GloveEncoderStatus, 10>, 5>;

/** @brief 手套校准同步调用的最终状态 */
enum class GloveCalibrationStatus : uint8_t {
    Success = 0xA0,
    Failed = 0xA1,
    Limited = 0xA2,
    Timeout = 0x00,
};

/** @brief 手套手指编号 */
enum class GloveFinger : uint8_t {
    Thumb = 0,  /**< 拇指 */
    Index = 1,  /**< 食指 */
    Middle = 2, /**< 中指 */
    Ring = 3,   /**< 无名指 */
    Pinky = 4,  /**< 小指 */
};

/** @brief 手套编码器校准掩码位，num 为编码器编号 0-9，位或组合后用于 CalibrateByMask */
#define ENCOS_GLOVE_CALI_E(num) ((uint16_t) (1U << (num)))

/**
 * @brief 表示一个 EtherCAT 手套从站的整手状态与校准接口
 *
 * 使用 `BaseAdapter::GetGlove(slave_idx)` 获取。对象由驱动管理器拥有，应用程序不得
 * 直接释放；需要销毁时调用 `DeleteGlove()`，调用后不得再保存或访问该指针。
 */
class ENCOS_BASE_API Glove {
    friend class EncosDriverManager;

private:
    explicit Glove(std::array<Bus*, 5> buses);

public:
    ~Glove();

    /**
     * @brief 获取最近一次合法接收的手套状态快照
     * @return 各编码器的最近状态；从未收到合法帧或最近状态无效时为 nullopt
     */
    GloveStatus GetStatus();

    /**
     * @brief 设置完整手套状态更新回调
     *
     * 当全部编码器自上次通知后均产生一次状态更新时，回调由适配器接收线程同步
     * 调用。回调不得阻塞，也不得调用 `DeleteGlove()` 或 `DeleteAdapter()`；传入空
     * 函数可取消注册。回调抛出的异常不会传播到接收线程。
     *
     * @param callback 状态回调
     */
    void SetOnStatus(std::function<void(const GloveStatus&)> callback);

    /**
     * @brief 同步请求校准全部 50 个编码器
     *
     * 按手指编号 0 至 4 依次校准，每个手指最多等待 5 ms；任一手指超时后立即
     * 停止后续校准并返回 Timeout。不得从接收回调或状态回调中调用。
     *
     * @return 全部手指响应时按失败、限流、成功优先级合并的结果；任一手指超时时
     *         返回 Timeout
     * @throws std::runtime_error 手套正在销毁或已不可用时抛出
     */
    GloveCalibrationStatus CalibrateAll();

    /**
     * @brief 同步请求按掩码校准指定手指上的多个编码器
     * @param finger_idx 手指编号，范围为 0-4
     * @param encoder_mask 编码器掩码，低 10 位分别对应编码器 0-9，可用 ENCOS_GLOVE_CALI_E 位或构造
     * @return 首个有效响应的状态；5 ms 超时时返回 Timeout
     * @throws std::invalid_argument 编号或掩码超出协议范围时抛出
     * @throws std::runtime_error 手套正在销毁或已不可用时抛出
     */
    GloveCalibrationStatus CalibrateByMask(uint8_t finger_idx, uint16_t encoder_mask);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos
