// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

#include "adapter/base_adapter.h"
#include "bus/bus.h"
#include "device/battery/battery.h"
#include "device/device.h"
#include "device/glove/glove.h"
#include "device/imu/imu.h"
#include "device/motor/motor.h"
#include "device/pms/pms.h"
#include "encos/driver_manager.h"
#include "encos/export.h"
#include "utils/log_writer.h"
#include "utils/port.h"

namespace encos {

/**
 * @brief 获取当前加载的 libencosdriver 版本。
 * @return 指向库内静态版本字符串的指针；调用方不得释放或修改。
 */
extern "C" ENCOS_API const char* encos_get_version();

/**
 * @brief 创建适配器实例
 * @param adapter_type 适配器类型名称（如 "Ethercat", "Can", "UsbSerial" 等）
 * @param interface_name 网络接口名称（如 "eth0", "can0" 等）
 * @param logger_name 日志记录器名称，默认为空则自动生成
 * @param log_level 日志级别，默认为 info
 * @return 非拥有的适配器裸指针
 * @throws std::runtime_error 如果插件加载失败
 */
ENCOS_API BaseAdapter* MakeAdapter(const std::string& adapter_type,
                                   const std::string& interface_name,
                                   const std::string& logger_name = "",
                                   LogLevel log_level = LogLevel::Info);

/**
 * @brief 创建适配器实例并传入扩展配置
 * @param adapter_type 适配器类型名称（如 "Ethercat", "Can", "UsbSerial" 等）
 * @param interface_name 网络接口名称（如 "eth0", "can0" 等）
 * @param logger_name 日志记录器名称，默认为空则自动生成
 * @param log_level 日志级别，默认为 info
 * @param extend 适配器自定义扩展配置，默认空对象 `{}`；字段语义由各插件自行定义
 * @return 非拥有的适配器裸指针
 * @throws std::runtime_error `extend` 不是 JSON 对象或插件加载失败
 *
 * 与同名四参数重载是两个独立符号，既有调用方无需重新编译。适配器身份仍只由
 * `interface_name` 决定：命中既有实例时，`extend` 中 `buses` 的条目会经
 * `BaseAdapter::UpdateBusExtend` 增量应用到该实例；顶层公共参数无法重新应用，会被忽略
 * 并记录诊断。只有实现了 `MakeAdapterV2` 的插件会收到该配置，其余插件按内置默认参数构造。
 */
ENCOS_API BaseAdapter* MakeAdapter(const std::string& adapter_type,
                                   const std::string& interface_name,
                                   const std::string& logger_name, LogLevel log_level,
                                   const nlohmann::json& extend);

/**
 * @brief 设置插件搜索路径
 * @param path 插件目录路径
 */
ENCOS_API void SetPluginPath(const std::string& path);

/**
 * @brief 获取所有可用的适配器类型
 * @return 适配器类型名称列表
 */
ENCOS_API std::vector<std::string> GetAvailableAdapterTypes();

/**
 * @brief 获取指定适配器类型可用的网络接口
 * @param adapter_type 适配器类型名称
 * @return 可用接口名称列表
 */
ENCOS_API std::vector<std::string> GetAvailableInterface(const std::string& adapter_type);

/**
 * @brief 清除所有日志记录器并关闭日志系统
 */
ENCOS_API void ClearLogger();

/**
 * @brief 兼容旧卸载入口并级联销毁适配器对象树
 * @param interface_name 适配器接口名
 * @return 是否完成销毁
 *
 * 旧实现仅移出共享缓存；裸指针所有权模型无法保留该续命语义。
 * 调用成功后该适配器及其全部子对象指针立即失效，请迁移到 DeleteAdapter。
 */
[[deprecated("UnloadAdapterByInterfaceName now destroys the adapter subtree; use DeleteAdapter")]]
ENCOS_API bool UnloadAdapterByInterfaceName(const std::string& interface_name);

/**
 * @brief 删除适配器及其全部子对象
 * @param adapter 管理器拥有的适配器指针
 * @return 是否完成删除
 */
ENCOS_API bool DeleteAdapter(BaseAdapter* adapter);

/**
 * @brief 删除总线及其全部设备
 * @param bus 管理器拥有的总线指针
 * @return 是否完成删除
 */
ENCOS_API bool DeleteBus(Bus* bus);

/** @brief 删除管理器拥有的电机 */
ENCOS_API bool DeleteMotor(Motor* motor);

/** @brief 删除管理器拥有的电池设备 */
ENCOS_API bool DeleteBattery(Battery* battery);

/** @brief 删除管理器拥有的惯导设备 */
ENCOS_API bool DeleteImu(Imu* imu);

/** @brief 删除管理器拥有的电源管理设备 */
ENCOS_API bool DeletePms(Pms* pms);

/**
 * @brief 删除管理器拥有的外部设备插件设备
 * @param device 由 `Bus::GetDevice()` 或管理器返回的指针
 * @return 是否完成删除
 */
ENCOS_API bool DeleteDevice(Device* device);

/**
 * @brief 获取所有可用的外部设备插件类型
 * @return 设备类型名称列表
 *
 */
ENCOS_API std::vector<std::string> GetAvailableDeviceTypes();

/**
 * @brief 删除管理器拥有的手套整手视图
 * @param glove 由 `BaseAdapter::GetGlove()` 返回的指针
 * @return 删除是否完整完成
 *
 * 调用后不得再访问传入指针。
 */
ENCOS_API bool DeleteGlove(Glove* glove);

}  // namespace encos
