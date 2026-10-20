// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "device/device.h"

namespace encos {

/** @brief 当前支持的设备插件驱动 ABI 版本 */
inline constexpr std::uint32_t kDeviceDriverAbiVersion = 1u;

/** @brief 能力位：认领整条总线，接收本总线上所有未被其它设备路由命中的帧 */
inline constexpr std::uint32_t kDeviceCapabilityClaimBus = 0x1u;

/**
 * @brief 整总线标识符：表示"使用整个 CAN 总线"的哨兵路由 ID
 *
 * 声明 `kDeviceCapabilityClaimBus` 的设备 MUST 在 `fill_route_ids` 中以本标识符作为唯一
 * 路由返回，宿主据此把它登记为所在总线的兜底接收者。取值落在 CAN 扩展帧 29 位 ID 空间
 * （上限 `0x1FFFFFFF`）之外，因此永不可能与真实 CAN ID 冲突；宿主 MUST NOT 把它当作精确
 * 路由键发布。
 */
inline constexpr std::uint32_t kDeviceClaimBusRouteId = std::numeric_limits<std::uint32_t>::max();

/**
 * @brief 外部设备插件描述符 v1
 *
 * 插件 MUST 由 `MakeDeviceDriver()` 返回指向静态存储期的本结构。
 */
struct DeviceDriverDescriptorV1 {
    /** 插件 ABI 版本，核心 MUST 校验后才会使用其余字段 */
    std::uint32_t abi_version = kDeviceDriverAbiVersion;
    /** 设备类型名，跨插件唯一 */
    const char* type_name = nullptr;
    /**
     * 能力位；未知位 MUST 被忽略以保持前向兼容
     *
     * 认领总线（`kDeviceCapabilityClaimBus`）使设备接收本总线上所有未被其它设备路由命中的
     * 帧（含未命中路由的系统帧 `0x7FF`），同一总线至多一个认领设备。
     */
    std::uint32_t capabilities = 0u;
    /**
     * @brief 填充该设备占用的全部路由 ID
     *
     * 宿主会先以 `out == nullptr`、`capacity == 0` 调用一次探测所需数量，随后再以足量缓冲
     * 调用一次。实现 MUST 在 `out == nullptr` 或 `capacity == 0` 时只返回总数而不写入
     * `out`，且 MUST NOT 解引用空指针。返回总数 MUST 对同一配置保持稳定。
     *
     * @param config_json 宿主规范化序列化后的设备配置 JSON 字符串；非空、调用期内有效
     * @param out 输出缓冲；可为 `nullptr`（数量探测）
     * @param capacity `out` 可容纳的 ID 数量；`out` 为空时为 0
     * @return 该设备实际需要的路由 ID 总数
     */
    std::size_t (*fill_route_ids)(const char* config_json, std::uint32_t* out,
                                  std::size_t capacity) = nullptr;
    /**
     * @brief 构造设备实例
     *
     * 该入口跨插件 ABI 边界，MUST NOT 抛出异常；失败时返回 `nullptr` 由核心回滚。
     * 构造期间不得获取设备在飞操作保护或依赖已发布的路由。
     */
    Device* (*create)(const DeviceCreateContext& context) noexcept = nullptr;
    /** @brief 释放设备实例 */
    void (*destroy)(Device* device) noexcept = nullptr;
};

}  // namespace encos

/**
 * @brief 设备插件导出入口的跨平台可见性宏
 *
 * Windows 动态插件在定义 `ENCOS_PLUGIN_EXPORTS` 时导出 `MakeDeviceDriver`，否则该入口
 * 不会出现在 DLL 导出表中，运行期 `GetProcAddress` 将失败。Linux 保持默认可见性。
 */
#if defined(_WIN32) && defined(ENCOS_PLUGIN_EXPORTS)
#define ENCOS_DEVICE_PLUGIN_API __declspec(dllexport)
#elif defined(__GNUC__)
#define ENCOS_DEVICE_PLUGIN_API __attribute__((visibility("default")))
#else
#define ENCOS_DEVICE_PLUGIN_API
#endif

/**
 * @brief 设备插件导出入口
 * @return 指向插件静态描述符的指针
 */
extern "C" ENCOS_DEVICE_PLUGIN_API const encos::DeviceDriverDescriptorV1* MakeDeviceDriver();
