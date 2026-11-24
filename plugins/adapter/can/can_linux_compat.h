// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <linux/can.h>

// Linux UAPI 的固定位值；允许使用尚未声明该标志的旧版构建头文件。
#ifndef CANFD_FDF
#define CANFD_FDF 0x04
#endif
