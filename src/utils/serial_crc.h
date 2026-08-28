#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @brief 串口 CRC 校验工具类
 *
 * 提供简单的校验和计算功能，用于串口通信数据校验。
 */
class SerialCrc {
public:
    /**
     * @brief 计算校验和
     * @param input 输入数据
     * @return 校验和字节
     *
     * 使用简单的字节累加算法计算校验和。
     */
    static std::byte calc(const std::vector<std::byte>& input) {
        uint8_t byte_crc = 0;
        for (size_t i = 0; i < input.size(); i++) {
            byte_crc += static_cast<uint8_t>(input[i]);
        }
        return std::byte{byte_crc};
    }
};
