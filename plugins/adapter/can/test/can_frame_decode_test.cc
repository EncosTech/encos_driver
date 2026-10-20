// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "can_frame_decode.h"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <linux/can.h>

namespace encos {
namespace {

/** @brief 构造一帧 canfd_frame；flags 为内核投递时携带的 canfd_frame::flags */
canfd_frame MakeFrame(uint32_t can_id, uint8_t len, std::uint8_t flags = 0) {
    canfd_frame frame{};
    frame.can_id = can_id;
    frame.len = len;
    frame.flags = flags;
    for (uint8_t i = 0; i < len && i < sizeof(frame.data); ++i) {
        frame.data[i] = static_cast<uint8_t>(0xA0u + i);
    }
    return frame;
}

TEST(CanFrameDecodeTests, ClassicFrameDeliveredInCanfdFormatIsNotMarkedFd) {
    // 启用 CAN_RAW_FD_FRAMES 后，内核对经典帧同样以 struct canfd_frame 投递、recv 也返回
    // CANFD_MTU，但不会设置 CANFD_FDF。FD 判定若依据读取长度就会把经典帧误标为 FD。
    const auto frame = MakeFrame(0x3F4u, 8);
    const auto message = DecodeCanFrame(frame, CANFD_MTU);

    EXPECT_EQ(message.id, 0x3F4u);
    EXPECT_EQ(message.len, 8u);
    EXPECT_FALSE(CanFrameFlagsUseCanFd(message.frame_flags));
    EXPECT_EQ(message.data[0], 0xA0u);
}

TEST(CanFrameDecodeTests, FdFrameIsMarkedFd) {
    const auto frame = MakeFrame(0x123u, 8, CANFD_FDF | CANFD_BRS);
    const auto message = DecodeCanFrame(frame, CANFD_MTU);

    EXPECT_TRUE(CanFrameFlagsUseCanFd(message.frame_flags));
    EXPECT_TRUE(CanFrameFlagsHaveValidCanFdBits(message.frame_flags));
    EXPECT_EQ(message.len, 8u);
}

TEST(CanFrameDecodeTests, LongerFdPayloadIsClampedToTheFixedPayloadSize) {
    // 内部帧负载固定 8 字节：更长的 FD 帧按 8 字节截断（仓库既有限制，非本次改动引入）
    const auto frame = MakeFrame(0x123u, 12, CANFD_FDF);
    const auto message = DecodeCanFrame(frame, CANFD_MTU);

    EXPECT_EQ(message.len, 8u);
    EXPECT_EQ(message.data[7], 0xA7u);
}

TEST(CanFrameDecodeTests, ClassicSocketReadsAreNotMarkedFd) {
    // 未启用 CAN_RAW_FD_FRAMES 时 recv 返回 CAN_MTU，同样不应判为 FD
    const auto frame = MakeFrame(0x123u, 8);
    const auto message = DecodeCanFrame(frame, CAN_MTU);

    EXPECT_FALSE(CanFrameFlagsUseCanFd(message.frame_flags));
}

TEST(CanFrameDecodeTests, ExtendedIdIsMaskedAndMarked) {
    const auto frame = MakeFrame(CAN_EFF_FLAG | 0x0CF02D59u, 8);
    const auto message = DecodeCanFrame(frame, CANFD_MTU);

    EXPECT_EQ(message.id, 0x0CF02D59u);
    EXPECT_TRUE(CanFrameFlagsUseExtendedId(message.frame_flags));
    EXPECT_FALSE(CanFrameFlagsUseCanFd(message.frame_flags));
}

TEST(CanFrameDecodeTests, RtrFrameCarriesNoPayload) {
    const auto frame = MakeFrame(CAN_RTR_FLAG | 0x321u, 4);
    const auto message = DecodeCanFrame(frame, CANFD_MTU);

    EXPECT_EQ(message.id, 0x321u);
    EXPECT_TRUE(CanFrameFlagsUseRtr(message.frame_flags));
    for (const auto byte : message.data) {
        EXPECT_EQ(byte, 0u);
    }
}

}  // namespace
}  // namespace encos
