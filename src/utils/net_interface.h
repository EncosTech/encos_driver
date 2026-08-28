#pragma once

#include <string>
#include <vector>

namespace encos {

/**
 * @brief 检查字符串是否以指定前缀开头
 * @param str 要检查的字符串
 * @param prefix 前缀
 * @return 如果字符串以前缀开头则返回 true
 */
inline bool StartsWith(const std::string& str, const std::string& prefix) {
    return str.rfind(prefix, 0) == 0;
}

/**
 * @brief 获取所有有线网络接口名称
 * @return 有线接口名称列表（如 "eth0", "enp0s31f6" 等）
 */
std::vector<std::string> GetWiredInterfaceNames();

/**
 * @brief 获取所有 CAN 接口名称（仅 Linux）
 * @return CAN 接口名称列表（如 "can0", "vcan0" 等）
 *
 * 在非 Linux 平台上返回空列表
 */
std::vector<std::string> GetCanInterfaceNames();

}  // namespace encos