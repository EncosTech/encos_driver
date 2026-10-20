// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace encos::gs_usb {

/** @brief gs_usb HOST_FORMAT 请求的固定载荷 */
constexpr std::uint32_t kHostFormatMagic = 0x0000beefU;

/** @brief 经典 CAN 帧在 USB 线上的固定长度 */
constexpr std::size_t kClassicFrameSize = 20U;
/** @brief CAN FD 帧在 USB 线上的固定长度 */
constexpr std::size_t kFdFrameSize = 76U;
/** @brief 设备上报总线接收帧时使用的 echo_id */
constexpr std::uint32_t kEchoBusFrame = 0xFFFFFFFFU;

/** @brief can_id 字段中的标志位 */
constexpr std::uint32_t kCanEff = 0x80000000U;
constexpr std::uint32_t kCanRtr = 0x40000000U;
constexpr std::uint32_t kCanErr = 0x20000000U;
constexpr std::uint32_t kCanExtIdMask = 0x1FFFFFFFU;
constexpr std::uint32_t kCanStdIdMask = 0x7FFU;

/** @brief 帧标志字节（offset 10）中的位 */
constexpr std::uint8_t kFrameFlagFd = 0x02U;
constexpr std::uint8_t kFrameFlagBrs = 0x04U;
constexpr std::uint8_t kFrameFlagEsi = 0x08U;

/** @brief 设备能力位 */
constexpr std::uint32_t kFeatureListenOnly = 1U << 0;
constexpr std::uint32_t kFeatureLoopback = 1U << 1;
constexpr std::uint32_t kFeatureFd = 1U << 8;
constexpr std::uint32_t kFeatureBtConstExt = 1U << 10;
constexpr std::uint32_t kFeatureGetState = 1U << 13;

/** @brief 厂商控制请求码（bRequest） */
constexpr std::uint8_t kRqHostFormat = 0U;
constexpr std::uint8_t kRqBitTiming = 1U;
constexpr std::uint8_t kRqMode = 2U;
constexpr std::uint8_t kRqBtConst = 4U;
constexpr std::uint8_t kRqDeviceConfig = 5U;
constexpr std::uint8_t kRqDataBitTiming = 10U;
constexpr std::uint8_t kRqBtConstExt = 11U;
constexpr std::uint8_t kRqGetState = 14U;

/** @brief MODE 请求的 flags 位 */
constexpr std::uint32_t kModeListenOnly = 0x1U;
constexpr std::uint32_t kModeLoopback = 0x2U;
constexpr std::uint32_t kModeFd = 0x100U;

/** @brief 位定时参数；线上依次为 prop/phase1/phase2/sjw/brp */
struct BitTiming {
    std::uint32_t prop_seg = 0;
    std::uint32_t phase_seg1 = 0;
    std::uint32_t phase_seg2 = 0;
    std::uint32_t sjw = 0;
    std::uint32_t brp = 0;
};

/** @brief 设备返回的扩展位定时约束（gs_can_bt_const_extended） */
struct BtConstExt {
    std::uint32_t feature = 0;
    std::uint32_t fclk = 0;
    std::uint32_t tseg1_min = 0;
    std::uint32_t tseg1_max = 0;
    std::uint32_t tseg2_min = 0;
    std::uint32_t tseg2_max = 0;
    std::uint32_t sjw_max = 0;
    std::uint32_t brp_min = 0;
    std::uint32_t brp_max = 0;
    std::uint32_t brp_inc = 0;
    std::uint32_t dtseg1_min = 0;
    std::uint32_t dtseg1_max = 0;
    std::uint32_t dtseg2_min = 0;
    std::uint32_t dtseg2_max = 0;
    std::uint32_t dsjw_max = 0;
    std::uint32_t dbrp_min = 0;
    std::uint32_t dbrp_max = 0;
    std::uint32_t dbrp_inc = 0;
};

/** @brief 通道状态（GET_STATE） */
struct ChannelState {
    std::uint32_t state = 0;
    std::uint32_t rec = 0;
    std::uint32_t tec = 0;
};

/** @brief 线上帧；经典帧使用前 8 字节数据，FD 帧最多 64 字节 */
struct Frame {
    std::uint32_t echo_id = kEchoBusFrame;
    std::uint32_t can_id = 0;
    std::uint8_t dlc = 0;
    std::uint8_t channel = 0;
    std::uint8_t flags = 0;
    std::uint8_t reserved = 0;
    std::uint8_t data[64] = {};
};

/** @brief 读取小端 32 位整数 */
std::uint32_t ReadLe32(const std::uint8_t* data);
/** @brief 写入小端 32 位整数 */
void WriteLe32(std::uint8_t* data, std::uint32_t value);

/** @brief 解析 72 字节的 BT_CONST_EXT 响应 */
bool DecodeBtConstExt(const std::uint8_t* data, std::size_t size, BtConstExt* out);
/** @brief 按目标波特率与采样点计算位定时，失败返回空 */
std::optional<BitTiming> ComputeBitTiming(const BtConstExt& constraints, std::uint32_t bitrate,
                                          double sample_point, bool data_phase);
/** @brief 编码 20 字节位定时请求 */
bool EncodeBitTiming(const BitTiming& timing, std::uint8_t out[20]);

/** @brief 根据帧标志返回线上长度（FD 为 76，否则 20） */
std::size_t FrameSize(std::uint8_t flags);
/** @brief 编码帧到线上字节，容量不足返回 0 */
std::size_t EncodeFrame(const Frame& frame, std::uint8_t* out, std::size_t capacity);
/** @brief 从缓冲解析一帧，返回消耗字节数，数据不足或非法返回 0 */
std::size_t DecodeFrame(const std::uint8_t* data, std::size_t available, Frame* out);

}  // namespace encos::gs_usb
