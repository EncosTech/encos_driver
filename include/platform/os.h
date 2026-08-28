#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "encos/export.h"

namespace encos::platform {

namespace fs = std::filesystem;

ENCOS_BASE_API bool GetEnv(const std::string& name, std::string& out) noexcept;
ENCOS_BASE_API void SetEnv(const std::string& name, const std::string& value);
ENCOS_BASE_API fs::path PluginDir();
ENCOS_BASE_API fs::path PluginLibraryPath(const fs::path& plugin_dir,
                                          const std::string& adapter_type);
ENCOS_BASE_API std::vector<std::string> GetWiredInterfaceNames();
/**
 * @brief 获取可用的 IgH EtherCAT 主站 ID
 * @return 已存在的 /dev/EtherCAT<N> 字符设备对应的十进制主站 ID 列表
 */
ENCOS_BASE_API std::vector<std::string> GetIghMasterIds();
ENCOS_BASE_API std::vector<std::string> GetCanInterfaceNames();
ENCOS_BASE_API std::vector<std::string> GetSerialPortNames();

}  // namespace encos::platform
