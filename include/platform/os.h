#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "encos/export.h"

namespace encos::platform {

namespace fs = std::filesystem;

/** @brief 读取环境变量，存在时写入 `out` 并返回 true */
ENCOS_BASE_API bool GetEnv(const std::string& name, std::string& out) noexcept;
/** @brief 设置当前进程的环境变量 */
ENCOS_BASE_API void SetEnv(const std::string& name, const std::string& value);
/** @brief 获取运行时插件搜索目录 */
ENCOS_BASE_API fs::path PluginDir();
/** @brief 拼接指定适配器插件的动态库路径 */
ENCOS_BASE_API fs::path PluginLibraryPath(const fs::path& plugin_dir,
                                          const std::string& adapter_type);
/** @brief 获取可用的有线网络接口名称 */
ENCOS_BASE_API std::vector<std::string> GetWiredInterfaceNames();
/**
 * @brief 获取可用的 IgH EtherCAT 主站 ID
 * @return 已存在的 `/dev/EtherCAT<N>` 字符设备对应的十进制主站 ID 列表
 */
ENCOS_BASE_API std::vector<std::string> GetIghMasterIds();
/** @brief 获取可用的 SocketCAN 接口名称 */
ENCOS_BASE_API std::vector<std::string> GetCanInterfaceNames();
/** @brief 获取可用的串口设备名称 */
ENCOS_BASE_API std::vector<std::string> GetSerialPortNames();

}  // namespace encos::platform
