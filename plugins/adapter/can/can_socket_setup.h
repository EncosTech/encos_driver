// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>

#include "platform/log.h"

namespace encos::can {

/**
 * @brief CAN 位时序参数
 *
 * 成员初始化值即默认值；空 extend 与字段缺省都回落到这些值。
 */
struct CanTiming {
    int bitrate = 1000000;        /**< 仲裁段位率，bit/s */
    double sample_point = 0.765;  /**< 仲裁段采样点，0-1 小数 */
    int dbitrate = 5000000;       /**< CAN FD 数据段位率，bit/s */
    double dsample_point = 0.882; /**< CAN FD 数据段采样点，0-1 小数 */
};

/**
 * @brief 把位时序编码为单个命令行参数
 * @param timing 位时序
 * @return 形如 `1000000,0.765000,5000000,0.882000` 的字符串
 */
std::string FormatCanTiming(const CanTiming& timing);

/**
 * @brief 解析 FormatCanTiming 产出的字符串
 * @param text 待解析文本
 * @param timing 解析成功时写入；失败时不被修改
 * @return 是否解析成功
 */
bool ParseCanTiming(const std::string& text, CanTiming* timing);

bool HasRequiredRuntimeCapabilities(LoggerPtr logger);
bool InitializeCanInterface(const std::string& ifname, const CanTiming& timing, LoggerPtr logger);
bool EnsureCanInterfaceReady(const std::string& ifname, const CanTiming& timing, LoggerPtr logger);
int CreateConfiguredCanSocket(const std::string& ifname);

}  // namespace encos::can
