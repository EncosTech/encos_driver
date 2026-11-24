// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "device/motor/types.h"
#include "encos/export.h"

namespace encos {

class EncosDriverManager;

/** @brief 灵巧手网关的 CAN 采样点，单位为 0.1% */
struct DexHandSamplePoints {
    uint16_t nominal; /**< 仲裁域采样点，例如 750 表示 75.0% */
    uint16_t data;    /**< 数据域采样点，例如 875 表示 87.5% */
};

/** @brief 灵巧手管理请求结果 */
enum class DexHandStatus : uint8_t {
    Success = 0x00,          /**< 操作成功 */
    InvalidParameter = 0x01, /**< 固件拒绝请求参数 */
    SaveFailed = 0x02,       /**< 固件保存失败 */
    FirmwareBusy = 0x03,     /**< 固件升级期间忙 */
    Timeout,                 /**< 等待应答超时 */
    Canceled,                /**< 设备销毁时取消请求 */
    RequestInProgress,       /**< 此地址已有管理请求在等待应答 */
};

/** @brief 管理应答，成功时附有固件返回的参数 */
template <typename T>
struct DexHandReply {
    DexHandStatus status;
    std::optional<T> value;
};

/** @brief 管理请求使用的帧格式和应答等待时间 */
struct DexHandRequestOptions {
    bool canfd = false; /**< 使用标准 CAN FD 数据帧，否则使用经典 CAN */
    std::chrono::milliseconds timeout{500}; /**< 提交报文后等待应答的时间 */
};

/**
 * @brief 灵巧手网关的 CAN 管理接口，由总线按请求地址复用
 *
 * 请求立即提交，并阻塞等待应答。同一对象允许一个在飞请求。
 * 子电机通过 Bus 的子 ID 接口单独获取。
 */
class ENCOS_BASE_API DexHand {
    friend class EncosDriverManager;

public:
    ~DexHand();

    /** @brief 查询网关已保存的仲裁域和数据域采样点 */
    DexHandReply<DexHandSamplePoints> GetSamplePoints(DexHandRequestOptions options = {});

    /**
     * @brief 保存网关的仲裁域和数据域采样点
     * @param points 两个采样点，单位为 0.1%
     * @param options 帧格式和应答超时
     * @return 操作状态及成功时固件返回的采样点
     *
     * 网关使用当前时序应答，新采样点在网关重启后生效。
     */
    DexHandReply<DexHandSamplePoints> SetSamplePoints(DexHandSamplePoints points,
                                                      DexHandRequestOptions options = {});

    /** @brief 查询网关当前基址 */
    DexHandReply<uint8_t> GetBaseId(DexHandRequestOptions options = {});

    /**
     * @brief 保存网关基址，成功应答后立即使用新基址通信
     * @param new_base_id 新基址（1–247）
     * @param options 帧格式和应答超时
     * @return 操作状态及成功时固件返回的新基址
     * @throws std::invalid_argument 新基址超出范围
     *
     * 网关沿用请求 ID 应答。成功后用新基址重新取得管理对象及子电机对象。
     */
    DexHandReply<uint8_t> SetBaseId(int new_base_id, DexHandRequestOptions options = {});

private:
    DexHand(uint32_t request_id, std::function<void(const MotorPackMsg&)> writer);

    static bool IsReplyFrame(const MotorPackMsg& message) noexcept;
    void OnMessage(const MotorPackMsg& message);
    void CancelWaiters();
    DexHandReply<MotorPackMsg> Exchange(MotorPackMsg request, DexHandRequestOptions options);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace encos
