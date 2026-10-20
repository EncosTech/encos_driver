// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <future>
#include <mutex>
#include <optional>
#include <soem/soem.h>
#include <string>
#include <vector>

#include "ethercat_base_handle.h"
#include "ethercat_can_timing.h"

class EthercatHandle final : public EthercatBaseHandle {
public:
    static constexpr auto kDefaultLoopPeriod = std::chrono::microseconds(1000);

    /**
     * @brief 构造并立即初始化主站
     * @param ifname 网络接口名
     * @param logger 日志记录器
     * @param extend 已解析的扩展配置信封；给出位时序字段的通道在 SAFE_OP 下发 SDO
     */
    EthercatHandle(std::string ifname, LoggerPtr logger, AdapterExtend extend = {});
    ~EthercatHandle();

    void Send(const MotorMessage& message);
    void Send(const MotorMessages& messages);
    void SendSynchronized(const MotorMessages& messages);
    void Loop(std::chrono::microseconds period = kDefaultLoopPeriod);

    EthercatHandle(const EthercatHandle&) = delete;
    EthercatHandle& operator=(const EthercatHandle&) = delete;
    void RequestStop();
    /** @brief 调用方须先停止并等待 Loop 线程退出；降到 SAFEOP 后关闭端口 */
    void Stop();
    /** @brief 仅在运行且已确认 OP 与有效过程数据时返回 true */
    bool Ok() const;
    /** @brief 等待启动循环确认从站处于 OP 且过程数据 WKC 有效 */
    bool WaitUntilOperational(std::chrono::milliseconds timeout) const;

    /**
     * @brief 在循环线程上把 extend 中 `buses` 点名的通道重新下发时序
     * @param extend 已解析的增量配置信封
     * @return 全部点名通道都成功时返回 true
     * @throws std::runtime_error 信封指向不存在的从站或通道，或该实例已不可用
     *
     * SOEM 非线程安全，因此本函数只把请求交给循环线程执行，不自作 I/O。
     */
    bool ReconfigureBuses(const AdapterExtend& extend);

private:
    friend class EthercatHandleTestAccess;
    explicit EthercatHandle(LoggerPtr logger) : EthercatBaseHandle(std::move(logger)) {}
    bool Initialize();
    /** @brief 按 extend_ 逐通道下发 CAN 位时序 SDO；未配置时直接成功 */
    bool ApplyChannelTimings();
    /** @brief 写一个 SDO；失败时记录并返回 false */
    bool WriteSdo(int slave, std::uint16_t index, std::uint8_t subindex, std::uint32_t value,
                  std::uint8_t size);
    /** @brief 读一个 32 位 SDO；仅用于失败时的诊断，失败不改变调用结果 */
    bool ReadSdoU32(int slave, std::uint16_t index, std::uint8_t subindex, std::uint32_t* out);

    /**
     * @brief 把一条通道的时序下发到从站（只在循环线程上调用）
     * @param slave SOEM 从站索引（从 1 起）
     * @param channel CAN 通道号
     * @param override_json 该总线在 `buses` 中的覆盖条目
     * @return 下发并被从站接受时返回 true
     *
     * 固件先暂存子索引 1..4、由 `apply`（5）提交，所以这里先读回 1..4
     * 作为种子、用条目 覆盖后写全四项，再写 `apply=1`，最后读
     * `status`（6）判断是否被拒。
     */
    bool ApplyBusTiming(int slave, unsigned channel, const nlohmann::json& override_json);

    /** @brief 由循环线程取出并执行挂起的重配请求 */
    void ServicePendingReconfigure();
    /** @brief 让挂起的请求以异常结束，避免被阻塞的调用方活过关闭流程 */
    void FailPendingReconfigure(const char* reason);

    /** @brief 提取主站级看门狗选项，剩余 extend_ 只含 CAN 时序配置 */
    void ParseWatchdogOptions();
    /** @brief 在 PREOP 配置输出 SM 看门狗并读回验证；保留原分频 */
    bool ConfigureWatchdogs(int only_slave = 0);
    /** @brief 从站复位后的 PREOP→SAFEOP 回调；配置失败停止主站恢复 */
    static int RestoreWatchdog(ecx_contextt* context, uint16 slave);
    /** @brief 在同一个有限等待窗口内确认各从站退出 OP */
    void LeaveOperational();
    void CloseContext();
    bool TransitionToOperational();
    void CheckState();
    void ExchangeOnce();
    void DegradedHandler();
    void LogBadWkc();

    /** @brief 使用当前平台的网络后端打开 SOEM 端口 */
    bool OpenPort();

    void WriteOutputs(const OutputFrame& packets);
    MotorMessages ReadInputs();

    std::string ifname_;
    AdapterExtend extend_;
    ecx_contextt ctx_{};
    std::vector<char> io_map_;
    std::atomic<int> expected_wkc_{0};
    std::atomic<int> wkc_{0};
    std::atomic<int> wkc_error_count_{0};
    std::atomic<int> wkc_error_iteration_{0};
    uint8_t current_group_{0};
    std::atomic<bool> context_closed_{true};
    bool slaves_operational_{false};
    bool operational_requested_{false};
    unsigned watchdog_timeout_ms_{100};
    /** @brief 重配请求的交接：调用方入队并等待，循环线程执行 */
    std::mutex reconfigure_mutex_;
    struct ReconfigureRequest {
        AdapterExtend extend;
        std::promise<void> completion;
    };
    std::deque<ReconfigureRequest> pending_reconfigure_;
    /** @brief SOEM 调用边界；生产使用默认实现，测试可替换为确定性链路模拟 */
    struct IoOperations {
        decltype(&ecx_FPRD) read_register = ecx_FPRD;
        decltype(&ecx_FPWR) write_register = ecx_FPWR;
        decltype(&ecx_close) close = ecx_close;
        decltype(&ecx_readstate) read_state = ecx_readstate;
        decltype(&ecx_writestate) write_state = ecx_writestate;
        decltype(&ecx_statecheck) state_check = ecx_statecheck;
        decltype(&ecx_reconfig_slave) reconfigure = ecx_reconfig_slave;
        decltype(&ecx_recover_slave) recover = ecx_recover_slave;
        decltype(&ecx_send_processdata) send = ecx_send_processdata;
        decltype(&ecx_receive_processdata) receive = ecx_receive_processdata;
        decltype(&ecx_SDOwrite) sdo_write = ecx_SDOwrite;
        decltype(&ecx_SDOread) sdo_read = ecx_SDOread;
    } io_;
};
