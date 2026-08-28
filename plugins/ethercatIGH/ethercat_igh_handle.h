#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ecrt.h>
#include <mutex>
#include <string>
#include <vector>

#include "ethercat_base_handle.h"

/**
 * @brief IGH 主站版 EtherCAT handle（直连模式）。
 *
 * 该类负责主站相关逻辑：
 * 1) 通过 ecrt 初始化/激活 IGH master + domain；
 * 2) 建立 PDO 映射与偏移量表；
 * 3) 在实时循环中完成 domain 收发。
 *
 * 主站无关逻辑（消息打包、解包、回调/队列管理）全部复用 EthercatBaseHandle。
 */
class EthercatIGHHandle final : public EthercatBaseHandle {
public:
    /**
     * @param interface_name IGH master ID 的十进制字符串，例如 "0" -> master0。
     *                      为空或非纯数字时抛出异常。
     * @param logger        上层注入的日志器。
     */
    explicit EthercatIGHHandle(std::string interface_name, LoggerPtr logger);
    ~EthercatIGHHandle();

    /**
     * @brief 入队发送消息。真正下发在 Loop() 周期线程中执行。
     */
    void Send(const MotorMessage& message);
    void Send(const MotorMessages& messages);
    void SendSynchronized(const MotorMessages& messages);

    /**
     * @brief 周期收发循环。
     * @param period 循环周期。
     *               当 period <= 0 时按从站数量自动选择：
     *               1~5: 250us(4kHz), 6~10: 500us(2kHz), >10: 1000us(1kHz)。
     */
    void Loop(std::chrono::microseconds period = std::chrono::microseconds(1000));

    EthercatIGHHandle(const EthercatIGHHandle&) = delete;
    EthercatIGHHandle& operator=(const EthercatIGHHandle&) = delete;

    void Stop();

    /**
     * @brief 在周期线程退出后释放 IGH 主站资源。
     *
     * 可重复调用；调用方必须先请求 Stop 并等待 Loop 返回。
     */
    void Release();

private:
    /**
     * @brief 单个“电机槽位”在 process data 中的字节偏移。
     */
    struct MotorOffsets {
        unsigned int id{0};
        unsigned int frame_flags{0};
        unsigned int dlc{0};
        std::array<unsigned int, 8> data{};
    };

    /**
     * @brief 单个 slave 在 process data 中的全部偏移（输入 + 输出）。
     */
    struct SlaveOffsets {
        unsigned int out_motor_num{0};
        unsigned int out_can_ide{0};
        std::vector<MotorOffsets> out_motor{};

        unsigned int in_motor_num{0};
        unsigned int in_can_ide{0};
        std::vector<MotorOffsets> in_motor{};
    };

    bool Initialize();
    bool ConfigureDomainEntries();
    void ConfigureSlaveConfigs();
    MotorMessages ReadInputs();
    void WriteOutputs(const OutputFrame& packets);
    void ClearOutput(std::size_t slave);
    void ClearOutputs();
    void CheckMasterState();
    void CheckDomainState();
    void CheckSlaveStates();

    // ENC28 手套整包从站 (vendor/product)；与新固件一致。
    static constexpr uint32_t kSlaveVendorId = 0x0000ffea;
    static constexpr uint32_t kSlaveProductCode = 0x00000201;

    // 原始输入的 IGH master ID 字符串。
    std::string interface_name_;
    // IGH master index（ecrt_request_master 入参）。
    unsigned int master_index_{0};
    // 参与 PDO 映射的 slave 数量（来自主站实时扫描结果）。
    std::size_t slave_count_{1};

    // IGH 运行时对象。
    ec_master_t* master_{nullptr};
    ec_domain_t* domain_{nullptr};
    uint8_t* domain_pd_{nullptr};
    ec_master_state_t master_state_{};
    ec_domain_state_t domain_state_{};
    std::vector<ec_slave_config_t*> slave_config_handles_;
    std::vector<ec_slave_config_state_t> slave_states_;
    // 从站首次达到 OP 前，PREOP/SAFEOP 属于正常初始化过程。
    std::vector<bool> slave_reached_operational_;
    std::chrono::steady_clock::time_point last_slave_state_check_{};

    // 每个 slave 的 PDO 偏移缓存，避免循环中查表/解析对象字典。
    std::vector<SlaveOffsets> slave_offsets_;
    // 每个 slave 实际扫描到的 identity。IGH 注册 PDO entry 时必须匹配具体 slave。
    std::vector<uint32_t> slave_vendor_ids_;
    std::vector<uint32_t> slave_product_codes_;
    // 连续空闲周期下避免重复清零 PDO 输出区。
    bool outputs_cleared_{false};
    std::mutex release_mutex_;
};
