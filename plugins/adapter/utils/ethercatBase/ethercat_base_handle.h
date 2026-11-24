// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <soem/soem.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "device/motor/types.h"
#include "platform/log.h"
#include "platform/sync.h"
#include "utils/tracy.h"

#pragma pack(push, 1)

using namespace encos;

/** @brief EtherCAT 固定线上槽位；独立于内部消息容量，保持原 PDO 布局 */
struct EthercatCanSlot {
    uint32_t id;
    uint8_t frame_flags{0};
    uint8_t len;
    uint8_t data[8];
};

static_assert(sizeof(EthercatCanSlot) == 14, "EtherCAT slot size changed");
static_assert(offsetof(EthercatCanSlot, id) == 0, "EtherCAT id offset changed");
static_assert(offsetof(EthercatCanSlot, frame_flags) == 4, "EtherCAT flags offset changed");
static_assert(offsetof(EthercatCanSlot, len) == 5, "EtherCAT length offset changed");
static_assert(offsetof(EthercatCanSlot, data) == 6, "EtherCAT payload offset changed");

/**
 * @brief 2 路普通 CAN 总线 EtherCAT 消息结构（每路 3 个槽位）
 */
typedef struct {
    uint8_t motor_num = 0;    /**< 电机数量 */
    uint8_t can_ide = 0;      /**< CAN 扩展帧标志 */
    EthercatCanSlot motor[6]; /**< 电机数据包 */
} EthercatClassicCanMsg2;

/**
 * @brief 8 路普通 CAN 总线 EtherCAT 消息结构（每路 3 个槽位）
 */
typedef struct {
    uint8_t motor_num = 0;     /**< 电机数量 */
    uint8_t can_ide = 0;       /**< CAN 扩展帧标志 */
    EthercatCanSlot motor[24]; /**< 电机数据包 */
    uint8_t reserved[2];       /**< 保留字段 */
} EthercatClassicCanMsg8;

/**
 * @brief 8 路 CAN FD 总线 EtherCAT 网关消息结构（每路 8 个槽位）
 */
typedef struct {
    EthercatCanSlot motor[64]; /**< 电机数据包 */
} EthercatCanFdMsg8;

/**
 * @brief 8 路 CAN FD 总线 EtherCAT 网关消息结构（每路 10 个槽位）
 */
typedef struct {
    EthercatCanSlot motor[80]; /**< 电机数据包 */
} EthercatCanFdMsg8x10;

/**
 * @brief 3 路 CAN FD 总线 EtherCAT 网关消息结构（每路 8 个槽位）
 */
typedef struct {
    EthercatCanSlot motor[24]; /**< 电机数据包 */
} EthercatCanFdMsg3;

/** @brief 3 路 CAN FD 总线，每路 10 个固定小槽，支持跨槽分片 */
struct EthercatCanFdMsg3x10 {
    static constexpr std::size_t kBusCount = 3;
    static constexpr std::size_t kSlotsPerBus = 10;
    EthercatCanSlot motor[kBusCount * kSlotsPerBus];
};

/** @brief 手套从站 PDO：5 根手指各 10 个编码器槽位（共 50 槽），每槽位 14 字节 */
typedef struct {
    EthercatCanSlot motor[50];
} EthercatGloveSlots;

#pragma pack(pop)

static_assert(sizeof(EthercatClassicCanMsg2) == 86, "EthercatClassicCanMsg2 size changed");
static_assert(sizeof(EthercatClassicCanMsg8) == 340, "EthercatClassicCanMsg8 size changed");
static_assert(sizeof(EthercatCanFdMsg8) == 896, "EthercatCanFdMsg8 must match 896-byte PDO size");
static_assert(sizeof(EthercatCanFdMsg8x10) == 1120,
              "EthercatCanFdMsg8x10 must match 1120-byte PDO size");
static_assert(sizeof(EthercatCanFdMsg3) == 336, "EthercatCanFdMsg3 must match 336-byte PDO size");
static_assert(sizeof(EthercatCanFdMsg3x10) == 420,
              "EthercatCanFdMsg3x10 must match 420-byte PDO size");
static_assert(sizeof(EthercatGloveSlots) == 700, "EthercatGloveSlots must match 700-byte PDO size");

inline constexpr std::size_t kEthercatMaxMessageSize = sizeof(EthercatCanFdMsg8x10);
inline constexpr std::size_t kEthercatMaxIoMapSize = 1U << 20;

/**
 * @brief 校验 SOEM 从站 PDO/同步管理器描述的 I/O 映射上界
 * @param context SOEM 上下文
 * @param logger 日志记录器
 * @return 描述可确定且保守上界不超过限制时返回 true
 */
std::optional<std::size_t> ComputeEthercatIoMapUpperBound(const ecx_contextt& context,
                                                          LoggerPtr logger);

/**
 * @brief 校验 SOEM 实际映射长度是否为非空且位于已分配容量内
 * @param mapped_bytes SOEM 返回的实际映射字节数
 * @param capacity 已分配的 I/O map 容量
 * @return 映射长度有效时返回 true
 */
bool IsEthercatMappedSizeValid(int mapped_bytes, std::size_t capacity);

inline bool ValidateEthercatIoMap(const ecx_contextt& context, LoggerPtr logger) {
    return ComputeEthercatIoMapUpperBound(context, std::move(logger)).has_value();
}

struct EthercatOutputBuffer {
    std::array<uint8_t, kEthercatMaxMessageSize> storage{};
    std::size_t payload_size{0};
    bool active{false};

    bool empty() const {
        return !active;
    }
    std::size_t size() const {
        return active ? payload_size : 0;
    }
    uint8_t* data() {
        return storage.data();
    }
    const uint8_t* data() const {
        return storage.data();
    }

    void reset(std::size_t size) {
        active = size != 0;
        payload_size = active ? size : 0;
        if (active) {
            std::memset(storage.data(), 0, payload_size);
        }
    }

    void clear() {
        active = false;
        payload_size = 0;
    }
};

using EthercatOutputFrame = std::vector<EthercatOutputBuffer>;

/**
 * @brief EtherCAT 句柄基础类，封装公共编解码、回调与发送队列能力
 *
 * 供多个 EtherCAT 插件共享，用于复用报文打包/解包、回调和发送队列管理。
 * 不负责 SOEM 主站初始化、状态迁移与循环驱动。
 */
class EthercatBaseHandle {
public:
    using ReceiveCallback = std::function<void(const MotorMessages&)>;

    /**
     * @brief 构造基础句柄
     * @param logger 日志记录器
     */
    explicit EthercatBaseHandle(LoggerPtr logger);
    virtual ~EthercatBaseHandle() = default;

    /**
     * @brief 设置输入消息回调
     * @param callback 回调函数
     */
    void SetReceiveCallback(ReceiveCallback callback);

    /**
     * @brief 获取各从站映射出的总线数量
     * @return 总线数量列表（索引与从站一致）
     */
    std::vector<int> GetBusSizes() const;

protected:
    /** @brief 降级期间抑制发送队列高水位日志，仍保留队列裁剪。 */
    void SetSendQueueWarningsSuppressed(bool suppressed);
    /**
     * @brief 从站 I/O 报文格式类型
     */
    enum class SlaveFormat {
        None,             /**< 未识别 */
        ClassicCan2Bus,   /**< 2 路普通 CAN，每路 3 槽位 */
        ClassicCan8Bus,   /**< 8 路普通 CAN，每路 3 槽位 */
        CanFd8Bus,        /**< 8 路 CAN FD，每路 8 槽位 */
        CanFd8Bus10Slots, /**< 8 路 CAN FD，每路 10 槽位 */
        CanFd3Bus,        /**< 3 路 CAN FD，每路 8 槽位 */
        CanFd3Bus10Slots, /**< 3 路 CAN FD，每路 10 槽位，支持分片 */
        Glove             /**< 手套从站 PDO：50 个编码器槽位 + 1 个命令槽位 */
    };

    /**
     * @brief 单个从站的格式与容量配置
     */
    struct SlaveConfig {
        SlaveFormat format{SlaveFormat::None}; /**< 数据格式 */
        std::size_t bus_count{0};              /**< 总线数量 */
        std::size_t motors_per_bus{0};         /**< 每条总线每周期槽位数量 */
        std::size_t message_size{0};           /**< 报文大小 */
    };

    /**
     * @brief 按 PDO 字节数识别受支持的从站格式
     * @param output_pdo_size 输出 PDO 字节数
     * @param input_pdo_size 输入 PDO 字节数，420 字节分片格式要求输入输出长度相同
     * @return 精确匹配时返回对应配置，否则返回零 Bus 的未识别配置
     */
    static SlaveConfig ClassifyOutputPdoSize(std::size_t output_pdo_size,
                                             std::size_t input_pdo_size = 0);

    /**
     * @brief 判断从站配置是否对应受支持的 PDO 格式
     * @param config 从站配置
     * @return 需要创建 Bus 和配置 PDO 时返回 true
     */
    static bool HasSupportedPdo(const SlaveConfig& config);

    /** @brief 判断是否使用支持 CAN FD 分片的固定小槽布局。 */
    static bool UsesCanFdSlots(SlaveFormat format);

    using OutputFrame = EthercatOutputFrame;

    /**
     * @brief 将即时业务消息直接填入待发送帧空槽，并统一裁剪过载队列
     * @param message 业务消息
     */
    void QueueMessage(const MotorMessage& message);

    /**
     * @brief 将即时业务消息批量填入待发送帧空槽，并统一裁剪过载队列
     * @param messages 业务消息列表
     */
    void QueueMessages(const MotorMessages& messages);

    /**
     * @brief 将软同步批次填入前序普通消息的剩余槽位，并封闭该批次涉及的总线边界
     * @param messages 完整业务消息批次
     */
    void QueueSynchronizedMessages(const MotorMessages& messages);

    /**
     * @brief 将业务电机消息打包为新的 EtherCAT 输出帧序列
     * @param messages 业务消息列表
     * @param slave_count 当前从站数量
     * @return 按周期发送顺序组织的输出帧
     */
    std::vector<OutputFrame> PackMessages(const MotorMessages& messages,
                                          std::size_t slave_count) const;

    using InputClock = std::chrono::steady_clock;

    /**
     * @brief 从 EtherCAT 输入区解码为业务消息列表
     * @param inputs 每个从站的输入缓冲区指针
     * @param slave_count 当前从站数量
     * @param cycle_period 主站交换周期，用于限制分片重组期限
     * @param now 当前 PDO 快照的采样时刻
     * @return 解码后的业务消息列表
     */
    MotorMessages DecodeInputs(const std::vector<const uint8_t*>& inputs, std::size_t slave_count,
                               std::chrono::microseconds cycle_period,
                               InputClock::time_point now = InputClock::now());

    /** @brief 由接收线程在链路中断时丢弃全部未完成的输入分片。 */
    void ResetInputFragments();

    /**
     * @brief 从统一的待发送帧队列取出一帧
     * @param frame 输出参数，成功时写入帧数据
     * @return true 表示成功取出
     */
    bool PrepareNextFrame(OutputFrame& frame);

    /**
     * @brief 线程安全复制当前接收回调
     * @return 回调函数副本
     */
    ReceiveCallback CopyReceiveCallback() const;

    /** @brief 从站格式配置表 */
    std::vector<SlaveConfig> slave_configs_;
    /** @brief 主循环运行标志 */
    std::atomic<bool> running_{false};
    /** @brief EtherCAT 是否处于 OP 状态 */
    std::atomic<bool> in_operational_{false};
    /** @brief 日志记录器 */
    LoggerPtr logger_;

private:
    struct InputFragment {
        MotorPackMsg message{};
        InputClock::time_point deadline{};
        unsigned received{0};
        bool from_previous_pdo{false};
    };

    void DecodeFragmentedInputs(const uint8_t* input, std::size_t slave,
                                std::chrono::microseconds cycle_period, InputClock::time_point now,
                                MotorMessages& result);

    std::vector<std::vector<std::array<InputFragment, 5>>> input_fragments_;

    struct QueuedFrame {
        OutputFrame frame;
        std::unordered_map<int, std::size_t> bus_generations;
    };

    /**
     * @brief 按总线填充已有帧空槽，不覆盖已占用槽位；容量不足时追加新帧
     * @param bus_generations 软同步批次的总线代次；nullptr 表示即时请求，不受代次限制且不设置边界
     */
    void PackMessagesIntoQueue(
        const MotorMessages& messages, std::size_t slave_count, std::deque<QueuedFrame>& send_queue,
        const std::unordered_map<int, std::size_t>* bus_generations = nullptr) const;

    /** @brief 收集在当前从站拓扑中可参与打包的总线编码 */
    std::unordered_set<int> CollectPackableBuses(const MotorMessages& messages,
                                                 std::size_t slave_count) const;

    /** @brief 统一队列超过三帧时仅保留最后一帧，即时请求与控制消息均参与裁剪 */
    void TrimSendQueueLocked();

    /** @brief 到达时打包并裁剪；软同步批次完成后递增其涉及总线的代次 */
    void PackBatchIntoSegmentsLocked(const MotorMessages& messages, bool synchronized);

    /** @brief 回调读写互斥锁 */
    mutable ENCOS_TRACY_LOCKABLE(platform::Mutex, callback_mutex_, "EtherCAT::callback_mutex");
    /** @brief 接收回调函数 */
    ReceiveCallback receive_callback_;
    /** @brief 发送队列互斥锁 */
    mutable ENCOS_TRACY_LOCKABLE(platform::Mutex, send_mutex_, "EtherCAT::send_mutex");
    /** @brief 即时请求与已 commit 控制消息共用的待发送帧队列 */
    std::deque<QueuedFrame> send_frames_;
    /** @brief 每条总线当前的控制消息打包代次 */
    std::unordered_map<int, std::size_t> bus_generations_;
    std::atomic<bool> send_queue_warnings_suppressed_{false};
};
