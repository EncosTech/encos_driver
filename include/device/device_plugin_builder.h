// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "device/device.h"
#include "device/device_plugin.h"
#include "external/json.hpp"

namespace encos {

namespace device_plugin_detail {

/**
 * @brief 按插件唯一类型隔离的回调与描述符存储
 *
 * C ABI 回调没有 self/user_data 参数，故描述符必须与状态一一对应；每个 PluginTag 对应独立的
 * 存储与 trampoline，动态和静态链接均保持隔离；每种插件必须使用唯一类型。
 */
template <typename PluginTag>
struct PluginSlot {
    std::function<std::vector<std::uint32_t>(const nlohmann::json&)> routes;
    std::function<std::unique_ptr<Device>(const DeviceCreateContext&, const nlohmann::json&)>
        create;
    std::function<void(Device*)> destroy;
    DeviceDriverDescriptorV1 descriptor{};

    PluginSlot() {
        descriptor.abi_version = kDeviceDriverAbiVersion;
        descriptor.fill_route_ids = &TrampolineFillRouteIds;
        descriptor.create = &TrampolineCreate;
        descriptor.destroy = &TrampolineDestroy;
    }

    /** @brief 取本插件类型的唯一槽位 */
    static PluginSlot& Get() {
        static PluginSlot slot;
        return slot;
    }

    /** @brief 把宿主传入的配置串解析为 JSON；宿主总是传入规范化序列化结果 */
    static nlohmann::json Decode(const char* config_json) {
        return config_json != nullptr ? nlohmann::json::parse(config_json)
                                      : nlohmann::json::object();
    }

    static std::size_t TrampolineFillRouteIds(const char* config_json, std::uint32_t* out,
                                              std::size_t capacity) {
        auto& slot = Get();
        if (!slot.routes) {
            return 0;
        }
        const auto ids = slot.routes(Decode(config_json));
        if (out != nullptr && capacity > 0) {
            const std::size_t count = std::min(ids.size(), capacity);
            std::copy_n(ids.begin(), count, out);
        }
        return ids.size();
    }

    static Device* TrampolineCreate(const DeviceCreateContext& context) noexcept {
        auto& slot = Get();
        if (!slot.create) {
            return nullptr;
        }
        try {
            return slot.create(context, Decode(context.config.c_str())).release();
        } catch (...) {
            return nullptr;
        }
    }

    static void TrampolineDestroy(Device* device) noexcept {
        try {
            auto& slot = Get();
            if (slot.destroy) {
                slot.destroy(device);
            } else {
                delete device;
            }
        } catch (...) {}
    }
};

}  // namespace device_plugin_detail

/**
 * @brief 现代 C++ 设备插件构建器
 *
 * 把 C ABI 描述符翻译为以 `nlohmann::json`、lambda 与 `std::unique_ptr` 表达的现代接口；
 * 配置直接以 JSON 传递，不要求插件定义独立配置结构。典型用法：
 *
 * @code
 * extern "C" const encos::DeviceDriverDescriptorV1* MakeDeviceDriver() {
 *     static const auto plugin =
 *         encos::DevicePlugin<MySensor>("MySensor")
 *             .Routes([](const nlohmann::json& config) {
 *                 return std::vector<std::uint32_t>{
 *                     config.value("base_id", 0x400u)};
 *             })
 *             .Create([](const encos::DeviceCreateContext& ctx, const nlohmann::json& config) {
 *                 return std::make_unique<MySensor>(ctx, config);
 *             });
 *     return plugin.descriptor();
 * }
 * @endcode
 */
template <typename PluginTag>
class DevicePluginBuilder {
public:
    /**
     * @brief 声明设备类型名
     * @param type_name 设备类型名，跨插件唯一且与宿主请求名一致
     */
    explicit DevicePluginBuilder(const char* type_name) {
        device_plugin_detail::PluginSlot<PluginTag>::Get().descriptor.type_name = type_name;
    }

    /**
     * @brief 设置路由解析回调
     * @param routes 由配置计算本设备占用的全部路由 ID
     */
    DevicePluginBuilder& Routes(
        std::function<std::vector<std::uint32_t>(const nlohmann::json&)> routes) {
        device_plugin_detail::PluginSlot<PluginTag>::Get().routes = std::move(routes);
        return *this;
    }

    /**
     * @brief 设置构造回调
     * @param create 由配置构造设备实例；抛出异常会被包装层转为构造失败
     */
    DevicePluginBuilder& Create(
        std::function<std::unique_ptr<Device>(const DeviceCreateContext&, const nlohmann::json&)>
            create) {
        device_plugin_detail::PluginSlot<PluginTag>::Get().create = std::move(create);
        return *this;
    }

    /**
     * @brief 自定义释放回调
     * @param destroy 释放设备实例；缺省为 `delete`
     */
    DevicePluginBuilder& Destroy(std::function<void(Device*)> destroy) {
        device_plugin_detail::PluginSlot<PluginTag>::Get().destroy = std::move(destroy);
        return *this;
    }

    /**
     * @brief 设置能力位
     * @param bits 能力位；未知位由宿主忽略
     */
    DevicePluginBuilder& Capabilities(std::uint32_t bits) {
        device_plugin_detail::PluginSlot<PluginTag>::Get().descriptor.capabilities = bits;
        return *this;
    }

    /**
     * @brief 声明认领整条总线
     *
     * 置认领能力位，并把路由固定为唯一整总线标识符 `kDeviceClaimBusRouteId`。
     */
    DevicePluginBuilder& ClaimBus() {
        auto& slot = device_plugin_detail::PluginSlot<PluginTag>::Get();
        slot.descriptor.capabilities |= kDeviceCapabilityClaimBus;
        slot.routes = [](const nlohmann::json&) {
            return std::vector<std::uint32_t>{kDeviceClaimBusRouteId};
        };
        return *this;
    }

    /** @brief 取指向静态描述符的指针，供 `MakeDeviceDriver()` 返回 */
    const DeviceDriverDescriptorV1* descriptor() const {
        return &device_plugin_detail::PluginSlot<PluginTag>::Get().descriptor;
    }
};

/**
 * @brief 创建设备插件构建器
 * @param type_name 设备类型名
 * @return 链式构建器
 */
template <typename PluginTag>
DevicePluginBuilder<PluginTag> DevicePlugin(const char* type_name) {
    return DevicePluginBuilder<PluginTag>(type_name);
}

}  // namespace encos
