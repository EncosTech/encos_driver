// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once
#include <linux/types.h>

#define ENCOS_XDP_FRAME_SIZE 2048U
#define ENCOS_XDP_FRAME_COUNT 2048U
#define ENCOS_XDP_RX_SIZE 1024U
#define ENCOS_XDP_TX_SIZE 512U
#define ENCOS_XDP_UMEM_SIZE (ENCOS_XDP_FRAME_SIZE * ENCOS_XDP_FRAME_COUNT)

/** @brief XDP 过滤器配置，地址和端口均使用网络字节序。 */
struct encos_xdp_filter {
    __u32 local_ip;
    __u32 remote_ip;
    __u16 local_port;
    __u16 remote_port;
    __u32 queue;
};
/** @brief 通过私有 Unix socketpair 发送给初始化进程的配置。 */
struct encos_xdp_request {
    char interface_name[16];
    struct encos_xdp_filter filter;
};
/** @brief 初始化结果；成功时附带 AF_XDP 和共享 UMEM 的两个文件描述符。 */
struct encos_xdp_reply {
    __s32 error;
    char message[252];
};
