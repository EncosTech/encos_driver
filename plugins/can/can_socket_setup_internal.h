#pragma once

#include <string>
#include <vector>

namespace encos::can {

/**
 * @brief 从 `ip -details link show` 输出中解析出的 SocketCAN 接口配置
 */
struct CanInterfaceConfig {
    bool up = false;
    bool fd_on = false;
    int bitrate = 0;
    double sample_point = 0.0;
    int dbitrate = 0;
    double dsample_point = 0.0;
};

/**
 * @brief 解析 `ip -details link show` 输出，提取接口状态与位时序
 */
CanInterfaceConfig ParseCanDetails(const std::string& ip_details_output);

/**
 * @brief 判断解析出的配置是否已满足目标 CAN FD-capable 时序
 */
bool IsCanConfigMatchingTarget(const CanInterfaceConfig& config);

/**
 * @brief 构造配置 SocketCAN 接口的 `ip link set ... type can ...` 参数列表
 */
std::vector<std::string> BuildCanSetupCommandArgs(const std::string& ifname);

/**
 * @brief 构造将接口置为 up 的 `ip link set ... up` 参数列表
 */
std::vector<std::string> BuildCanUpCommandArgs(const std::string& ifname);

/**
 * @brief 构造将接口置为 down 的 `ip link set ... down` 参数列表
 */
std::vector<std::string> BuildCanDownCommandArgs(const std::string& ifname);

}  // namespace encos::can
