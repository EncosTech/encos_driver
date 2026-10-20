// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "adapter/adapter_extend.h"
#include "adapter/adapter_plugin.h"
#include "adapter/base_adapter.h"
#include "ethernet_management.h"
#include "ethernet_timing.h"
#include "ethernet_tx_queue.h"
#include "transport/transport.h"

namespace encos {
/** @brief 通过平台数据通道和 EMR1/UDP 连接具有 1–8 路 CAN 的网关。 */
class EthernetAdapter : public BaseAdapter {
public:
    /** @brief 只指定网卡名，广播发现slave并使用GetBus(slave_id,can_id)。 */
    EthernetAdapter(const std::string& interface_name, const std::string& logger_name,
                    LogLevel level, const AdapterExtend& extend);
    ~EthernetAdapter() override;
    std::unordered_map<int, Bus*> GetBuses() override;
    bool Ok() override;

    /**
     * @brief 按 `buses` 增量更新既有实例上被点名通道的 CAN 位时序
     * @param extend_json 宿主规范化的扩展配置串
     * @return 全部点名总线都更新成功时返回 true
     * @throws std::runtime_error 信封非法、总线不存在，或网关拒绝该时序
     *
     * 顶层公共参数被忽略并记录诊断。重配会中断目标总线上正在传输的报文，
     * 调用方应先停掉该总线的流量。
     */
    bool UpdateBusExtend(const std::string& extend_json) override;

protected:
    void Send(const MotorMessage& message) override;
    void SendSynchronized(const MotorMessages& messages) override;

private:
    friend class EthernetAdapterTestAccess;
    EthernetAdapter() : BaseAdapter("ethernet-test", "ethernet-test", LogLevel::Off) {}
    void Worker();
    /** @brief 检查编码总线号是否落在已握手的实际通道范围 */
    bool IsValidBus(int index) const;
    std::unique_ptr<ethernet::Transport> transport_;
    struct Peer {
        unsigned channel_count = 0;
        std::unique_ptr<ethernet::ManagementClient> management;
        ethernet::TransmitQueue pending;
        MotorMessages outgoing;
        std::chrono::steady_clock::time_point heartbeat{};
    };
    bool multi_ = false;
    std::map<unsigned, Peer> peers_;
    std::mutex mutex_;
    /** 每个从站实际通道的当前生效位时序（数组容量为 8），作为部分更新的合并种子 */
    std::map<unsigned, ethernet::GatewayChannelTimings> timings_;
    /** 只保护 timings_；不沿用发送热路径上的 mutex_ */
    std::mutex timings_mutex_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};
/** @brief 创建 Ethernet 插件适配器。 */
BaseAdapter* CreateEthernetAdapterStatic(const std::string& interface_name,
                                         const std::string& logger_name, LogLevel level);
/** @brief 枚举可用网卡接口。 */
void EnumerateEthernetInterfaces(AdapterInterfaceEmit emit, void* context);

/**
 * @brief 创建 Ethernet 插件适配器并接收 extend 配置串
 * @param interface_name 网卡名或 `网卡@IPv4[,端口]` 连接串
 * @param logger_name 日志记录器名称
 * @param level 日志级别
 * @param extend_json 宿主规范化的扩展配置串；空串等同空信封
 * @return 适配器指针
 * @throws std::runtime_error 信封结构非法、位时序字段非法、指向不存在的总线，或网关初始化失败
 *
 * 识别字段与 Can 插件统一：`bitrate`、`sample_point`、`dbitrate`、`dsample_point`；
 * 采样点以 0-1 小数给出，写入网关报文前换算为千分比整数。总线标识为 `<slave>:<channel>`
 * 或其等价裸索引，可逐通道覆盖，例如
 * `{"bitrate": 1000000, "buses": {"2:5": {"bitrate": 500000}}}`
 */
BaseAdapter* CreateEthernetAdapterStaticWithExtend(const std::string& interface_name,
                                                   const std::string& logger_name, LogLevel level,
                                                   const std::string& extend_json);
}  // namespace encos
