#pragma once

#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * @brief 串口枚举器
 *
 * 提供跨平台的串口设备枚举功能。
 * 在 Windows 上枚举 COM 端口，在 Linux 上枚举 ttyUSB 和 ttyACM 设备。
 */
class SerialEnumerator {
public:
    /**
     * @brief 获取所有可用的串口设备路径
     * @return 串口设备路径列表
     *
     * Windows: 返回所有 COM 端口名称
     * Linux: 返回所有 /dev/ttyUSB* 和 /dev/ttyACM* 设备路径
     */
    static std::vector<std::string> GetPorts() {
        std::vector<std::string> ports;

#ifdef _WIN32
        // Windows 实现：使用 QueryDosDevice 遍历所有 DOS 设备
        char target_path[5000];  // 缓冲区存储所有设备名
        if (QueryDosDeviceA(NULL, target_path, sizeof(target_path))) {
            char* current = target_path;
            while (*current) {
                std::string name(current);
                // 仅筛选以 COM 开头的设备
                if (name.find("COM") == 0) {
                    ports.push_back(name);
                }
                current += name.length() + 1;
            }
        }
#else
        // Linux 实现：扫描 /sys/class/tty 并过滤 USB 总线设备
        using namespace std::filesystem;
        const path tty_dir = "/sys/class/tty";

        if (exists(tty_dir)) {
            for (const auto& entry : directory_iterator(tty_dir)) {
                std::string name = entry.path().filename().string();

                if (name.find("ttyUSB") != 0 && name.find("ttyACM") != 0) {
                    continue;
                }

                path device_link = entry.path() / "device";
                if (exists(device_link) && is_symlink(device_link)) {
                    ports.push_back("/dev/" + name);
                }
            }
        }
#endif
        return ports;
    }
};
