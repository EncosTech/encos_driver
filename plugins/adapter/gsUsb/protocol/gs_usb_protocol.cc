// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "gs_usb_protocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace encos::gs_usb {

std::uint32_t ReadLe32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

void WriteLe32(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value & 0xFFU);
    data[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    data[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    data[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

bool DecodeBtConstExt(const std::uint8_t* data, std::size_t size, BtConstExt* out) {
    if (data == nullptr || out == nullptr || size < 72U) {
        return false;
    }
    std::uint32_t values[18];
    for (std::size_t i = 0; i < 18U; ++i) {
        values[i] = ReadLe32(data + i * 4U);
    }
    BtConstExt result;
    result.feature = values[0];
    result.fclk = values[1];
    result.tseg1_min = values[2];
    result.tseg1_max = values[3];
    result.tseg2_min = values[4];
    result.tseg2_max = values[5];
    result.sjw_max = values[6];
    result.brp_min = values[7];
    result.brp_max = values[8];
    result.brp_inc = values[9];
    result.dtseg1_min = values[10];
    result.dtseg1_max = values[11];
    result.dtseg2_min = values[12];
    result.dtseg2_max = values[13];
    result.dsjw_max = values[14];
    result.dbrp_min = values[15];
    result.dbrp_max = values[16];
    result.dbrp_inc = values[17];
    *out = result;
    return true;
}

std::optional<BitTiming> ComputeBitTiming(const BtConstExt& constraints, std::uint32_t bitrate,
                                          double sample_point, bool data_phase) {
    if (constraints.fclk == 0U || bitrate == 0U || sample_point <= 0.0 || sample_point >= 1.0) {
        return std::nullopt;
    }
    const std::uint32_t tseg1_min = data_phase ? constraints.dtseg1_min : constraints.tseg1_min;
    const std::uint32_t tseg1_max = data_phase ? constraints.dtseg1_max : constraints.tseg1_max;
    const std::uint32_t tseg2_min = data_phase ? constraints.dtseg2_min : constraints.tseg2_min;
    const std::uint32_t tseg2_max = data_phase ? constraints.dtseg2_max : constraints.tseg2_max;
    const std::uint32_t sjw_max = data_phase ? constraints.dsjw_max : constraints.sjw_max;
    const std::uint32_t brp_min = data_phase ? constraints.dbrp_min : constraints.brp_min;
    const std::uint32_t brp_max = data_phase ? constraints.dbrp_max : constraints.brp_max;
    const std::uint32_t brp_inc = data_phase ? constraints.dbrp_inc : constraints.brp_inc;
    if (brp_min == 0U || brp_inc == 0U || tseg2_min == 0U) {
        return std::nullopt;
    }

    const std::uint64_t fclk = constraints.fclk;
    const std::uint64_t target = bitrate;
    for (std::uint64_t brp = brp_min; brp <= brp_max; brp += brp_inc) {
        const std::uint64_t denominator = target * brp;
        if (denominator == 0U || (fclk % denominator) != 0U) {
            continue;
        }
        const std::uint64_t total = fclk / denominator;  // 每比特 quanta 数
        if (total < 1U + tseg1_min + tseg2_min) {
            continue;
        }
        std::int64_t desired =
            static_cast<std::int64_t>(std::llround(sample_point * static_cast<double>(total))) - 1;
        std::int64_t max_seg1 =
            static_cast<std::int64_t>(total) - 1 - static_cast<std::int64_t>(tseg2_min);
        if (desired < static_cast<std::int64_t>(tseg1_min)) {
            desired = tseg1_min;
        }
        if (desired > static_cast<std::int64_t>(tseg1_max)) {
            desired = tseg1_max;
        }
        if (desired > max_seg1) {
            desired = max_seg1;
        }
        if (desired < static_cast<std::int64_t>(tseg1_min)) {
            continue;
        }
        const std::uint32_t seg1 = static_cast<std::uint32_t>(desired);
        const std::uint32_t seg2 =
            static_cast<std::uint32_t>(total - 1U - static_cast<std::uint64_t>(seg1));
        if (seg2 < tseg2_min || seg2 > tseg2_max) {
            continue;
        }
        BitTiming result;
        result.prop_seg = 0U;
        result.phase_seg1 = seg1;
        result.phase_seg2 = seg2;
        result.sjw = std::max(1U, std::min({seg1, seg2, sjw_max}));
        result.brp = static_cast<std::uint32_t>(brp);
        return result;
    }
    return std::nullopt;
}

bool EncodeBitTiming(const BitTiming& timing, std::uint8_t out[20]) {
    if (out == nullptr) {
        return false;
    }
    WriteLe32(out, timing.prop_seg);
    WriteLe32(out + 4, timing.phase_seg1);
    WriteLe32(out + 8, timing.phase_seg2);
    WriteLe32(out + 12, timing.sjw);
    WriteLe32(out + 16, timing.brp);
    return true;
}

std::size_t FrameSize(std::uint8_t flags) {
    return (flags & kFrameFlagFd) != 0U ? kFdFrameSize : kClassicFrameSize;
}

std::size_t EncodeFrame(const Frame& frame, std::uint8_t* out, std::size_t capacity) {
    if (out == nullptr) {
        return 0U;
    }
    const std::size_t size = FrameSize(frame.flags);
    if (capacity < size) {
        return 0U;
    }
    const std::size_t data_size = (frame.flags & kFrameFlagFd) != 0U
                                      ? static_cast<std::size_t>(frame.dlc)
                                      : std::min<std::size_t>(frame.dlc, 8U);
    if (data_size > sizeof(frame.data)) {
        return 0U;
    }
    std::memset(out, 0, size);
    WriteLe32(out, frame.echo_id);
    WriteLe32(out + 4, frame.can_id);
    out[8] = frame.dlc;
    out[9] = frame.channel;
    out[10] = frame.flags;
    std::memcpy(out + 12, frame.data, data_size);
    return size;
}

std::size_t DecodeFrame(const std::uint8_t* data, std::size_t available, Frame* out) {
    if (data == nullptr || out == nullptr || available < kClassicFrameSize) {
        return 0U;
    }
    const std::uint8_t flags = data[10];
    if ((flags & ~(kFrameFlagFd | kFrameFlagBrs | kFrameFlagEsi)) != 0U) {
        return 0U;
    }
    const std::size_t size = FrameSize(flags);
    if (available < size) {
        return 0U;
    }
    const std::uint32_t can_id = ReadLe32(data + 4);
    const std::uint8_t dlc = data[8];
    if ((can_id & kCanEff) == 0U && (can_id & kCanExtIdMask) > kCanStdIdMask) {
        return 0U;
    }
    if ((flags & kFrameFlagFd) != 0U) {
        if (dlc > 15U || (can_id & kCanRtr) != 0U) {
            return 0U;
        }
    } else if (dlc > 8U || (flags & (kFrameFlagBrs | kFrameFlagEsi)) != 0U) {
        return 0U;
    }
    out->echo_id = ReadLe32(data);
    out->can_id = can_id;
    out->dlc = dlc;
    out->channel = data[9];
    out->flags = flags;
    out->reserved = 0U;
    std::memset(out->data, 0, sizeof(out->data));
    const std::size_t data_size = (flags & kFrameFlagFd) != 0U ? static_cast<std::size_t>(dlc)
                                                               : std::min<std::size_t>(dlc, 8U);
    std::memcpy(out->data, data + 12, data_size);
    return size;
}

}  // namespace encos::gs_usb
