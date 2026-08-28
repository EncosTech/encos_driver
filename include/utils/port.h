#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace encos {

/**
 * @brief Port 默认使用的 CAN 帧数据
 *
 * CAN ID 由 BasePort 保存；该结构仅保存帧负载、帧标志和实际长度。
 */
struct PortFrame {
    std::array<std::uint8_t, 8> data{}; /**< 帧负载 */
    std::uint8_t frame_flags{0};        /**< CAN 帧标志 */
    std::uint8_t len{0};                /**< 实际帧长度 */
};

/**
 * @brief 比较两个默认 Port 帧
 * @param lhs 左侧帧
 * @param rhs 右侧帧
 * @return 所有字段相同时返回 true
 */
inline bool operator==(const PortFrame& lhs, const PortFrame& rhs) noexcept {
    return lhs.data == rhs.data && lhs.frame_flags == rhs.frame_flags && lhs.len == rhs.len;
}

/**
 * @brief 固定容量消息 Port 的多态基类
 *
 * Port 是严格的单生产者、单消费者结构。CAN ID 在构造后不可修改。
 */
class BasePort {
public:
    /** @brief 表示 Port 可保存不同 CAN ID 的消息 */
    static constexpr std::uint32_t kAnyCanId = std::numeric_limits<std::uint32_t>::max();

    /**
     * @brief 构造指定 CAN ID 的 Port
     * @param can_id 该 Port 对应的 CAN ID
     */
    explicit BasePort(std::uint32_t can_id) noexcept : can_id_(can_id) {}

    virtual ~BasePort() = default;

    BasePort(const BasePort&) = delete;
    BasePort& operator=(const BasePort&) = delete;
    BasePort(BasePort&&) = delete;
    BasePort& operator=(BasePort&&) = delete;

    /**
     * @brief 获取构造时指定的 CAN ID
     * @return 不可修改的 CAN ID
     */
    std::uint32_t GetCanId() const noexcept {
        return can_id_;
    }

    /** @brief 清除调用点之前已经发布的未读消息 */
    virtual void Clear() noexcept = 0;

private:
    const std::uint32_t can_id_;
};

namespace detail {

using PortAtomicWord = std::atomic<std::uint32_t>;
static_assert(PortAtomicWord::is_always_lock_free,
              "Port requires always-lock-free 32-bit atomic metadata");

constexpr std::uint32_t kPortCellCount = 3U;
constexpr std::uint32_t kPortNoHazard = kPortCellCount;
/** @brief 用于隔离 SPSC 两端热写字段的目标缓存行大小 */
constexpr std::size_t kPortCacheLineSize = 64U;
using PortTestHook = void (*)(void*);

struct PortNoInstrumentation {
    void Step() noexcept {}

    PortTestHook AfterSlotOddHook() const noexcept {
        return nullptr;
    }
    void* AfterSlotOddContext() const noexcept {
        return nullptr;
    }
    PortTestHook AfterSlotActiveHook() const noexcept {
        return nullptr;
    }
    void* AfterSlotActiveContext() const noexcept {
        return nullptr;
    }
    PortTestHook AfterHeadOddHook() const noexcept {
        return nullptr;
    }
    void* AfterHeadOddContext() const noexcept {
        return nullptr;
    }
    PortTestHook BeforeSlotHazardHook() const noexcept {
        return nullptr;
    }
    void* BeforeSlotHazardContext() const noexcept {
        return nullptr;
    }
    PortTestHook AfterSlotHazardHook() const noexcept {
        return nullptr;
    }
    void* AfterSlotHazardContext() const noexcept {
        return nullptr;
    }
    PortTestHook BeforeHeadHazardHook() const noexcept {
        return nullptr;
    }
    void* BeforeHeadHazardContext() const noexcept {
        return nullptr;
    }
};

struct PortTestInstrumentation {
    void Step() noexcept {
        ++atomic_steps;
    }

    PortTestHook AfterSlotOddHook() const noexcept {
        return after_slot_odd_hook;
    }
    void* AfterSlotOddContext() const noexcept {
        return after_slot_odd_context;
    }
    PortTestHook AfterSlotActiveHook() const noexcept {
        return after_slot_active_hook;
    }
    void* AfterSlotActiveContext() const noexcept {
        return after_slot_active_context;
    }
    PortTestHook AfterHeadOddHook() const noexcept {
        return after_head_odd_hook;
    }
    void* AfterHeadOddContext() const noexcept {
        return after_head_odd_context;
    }
    PortTestHook BeforeSlotHazardHook() const noexcept {
        return before_slot_hazard_hook;
    }
    void* BeforeSlotHazardContext() const noexcept {
        return before_slot_hazard_context;
    }
    PortTestHook AfterSlotHazardHook() const noexcept {
        return after_slot_hazard_hook;
    }
    void* AfterSlotHazardContext() const noexcept {
        return after_slot_hazard_context;
    }
    PortTestHook BeforeHeadHazardHook() const noexcept {
        return before_head_hazard_hook;
    }
    void* BeforeHeadHazardContext() const noexcept {
        return before_head_hazard_context;
    }

    std::uint64_t atomic_steps{0U};
    PortTestHook after_slot_odd_hook{nullptr};
    void* after_slot_odd_context{nullptr};
    PortTestHook after_slot_active_hook{nullptr};
    void* after_slot_active_context{nullptr};
    PortTestHook after_head_odd_hook{nullptr};
    void* after_head_odd_context{nullptr};
    PortTestHook before_slot_hazard_hook{nullptr};
    void* before_slot_hazard_context{nullptr};
    PortTestHook after_slot_hazard_hook{nullptr};
    void* after_slot_hazard_context{nullptr};
    PortTestHook before_head_hazard_hook{nullptr};
    void* before_head_hazard_context{nullptr};
};

struct PortSequenceSnapshot {
    std::uint64_t sequence;
};

struct PortSlotSnapshot {
    std::uint64_t sequence;
    std::uint32_t cell;
};

template <std::size_t Len, typename Message, typename Instrumentation = PortTestInstrumentation>
class PortTestAccess;

class PortSequenceMetadata {
public:
    template <typename Instrumentation>
    void Prepare(std::uint64_t sequence, Instrumentation& instrumentation,
                 PortTestHook after_odd_hook, void* hook_context) noexcept {
        const std::uint32_t next_version = writer_version_ + 2U;
        version_.store(next_version - 1U, std::memory_order_seq_cst);
        instrumentation.Step();
        if (after_odd_hook != nullptr) {
            after_odd_hook(hook_context);
        }
        sequence_low_.store(static_cast<std::uint32_t>(sequence), std::memory_order_seq_cst);
        instrumentation.Step();
        sequence_high_.store(static_cast<std::uint32_t>(sequence >> 32U),
                             std::memory_order_seq_cst);
        instrumentation.Step();
        version_.store(next_version, std::memory_order_seq_cst);
        instrumentation.Step();
        writer_version_ = next_version;
    }

    std::optional<PortSequenceSnapshot> ReadProtected() const noexcept {
        const std::uint32_t version_before = version_.load(std::memory_order_seq_cst);
        const std::uint32_t sequence_low = sequence_low_.load(std::memory_order_seq_cst);
        const std::uint32_t sequence_high = sequence_high_.load(std::memory_order_seq_cst);
        const std::uint32_t version_after = version_.load(std::memory_order_seq_cst);
        if ((version_before & 1U) != 0U || version_before != version_after) {
            return std::nullopt;
        }
        return PortSequenceSnapshot{static_cast<std::uint64_t>(sequence_low) |
                                    (static_cast<std::uint64_t>(sequence_high) << 32U)};
    }

    void ForceVersionNearWrap() noexcept {
        writer_version_ = std::numeric_limits<std::uint32_t>::max() - 1U;
        version_.store(writer_version_, std::memory_order_seq_cst);
    }

private:
    PortAtomicWord version_{0U};
    PortAtomicWord sequence_low_{0U};
    PortAtomicWord sequence_high_{0U};
    std::uint32_t writer_version_{0U};
};

class PortSlotMetadata {
public:
    template <typename Instrumentation>
    void Prepare(std::uint64_t sequence, std::uint32_t cell, Instrumentation& instrumentation,
                 PortTestHook after_odd_hook, void* hook_context) noexcept {
        const std::uint32_t next_version = writer_version_ + 2U;
        version_.store(next_version - 1U, std::memory_order_seq_cst);
        instrumentation.Step();
        if (after_odd_hook != nullptr) {
            after_odd_hook(hook_context);
        }
        sequence_low_.store(static_cast<std::uint32_t>(sequence), std::memory_order_seq_cst);
        instrumentation.Step();
        sequence_high_.store(static_cast<std::uint32_t>(sequence >> 32U),
                             std::memory_order_seq_cst);
        instrumentation.Step();
        cell_.store(cell, std::memory_order_seq_cst);
        instrumentation.Step();
        version_.store(next_version, std::memory_order_seq_cst);
        instrumentation.Step();
        writer_version_ = next_version;
    }

    std::optional<PortSlotSnapshot> ReadProtected() const noexcept {
        const std::uint32_t version_before = version_.load(std::memory_order_seq_cst);
        const std::uint32_t sequence_low = sequence_low_.load(std::memory_order_seq_cst);
        const std::uint32_t sequence_high = sequence_high_.load(std::memory_order_seq_cst);
        const std::uint32_t cell = cell_.load(std::memory_order_seq_cst);
        const std::uint32_t version_after = version_.load(std::memory_order_seq_cst);
        if ((version_before & 1U) != 0U || version_before != version_after) {
            return std::nullopt;
        }
        return PortSlotSnapshot{static_cast<std::uint64_t>(sequence_low) |
                                    (static_cast<std::uint64_t>(sequence_high) << 32U),
                                cell};
    }

    void ForceVersionNearWrap() noexcept {
        writer_version_ = std::numeric_limits<std::uint32_t>::max() - 1U;
        version_.store(writer_version_, std::memory_order_seq_cst);
    }

private:
    PortAtomicWord version_{0U};
    PortAtomicWord sequence_low_{0U};
    PortAtomicWord sequence_high_{0U};
    PortAtomicWord cell_{0U};
    std::uint32_t writer_version_{0U};
};

class PortHeadPublication {
public:
    template <typename Instrumentation>
    void Publish(std::uint64_t sequence, Instrumentation& instrumentation,
                 PortTestHook after_odd_hook, void* hook_context) noexcept {
        const std::uint32_t hazard = reader_hazard_.load(std::memory_order_seq_cst);
        instrumentation.Step();
        std::uint32_t next_cell = (producer_active_ + 1U) % kPortCellCount;
        if (next_cell == hazard) {
            next_cell = (next_cell + 1U) % kPortCellCount;
        }
        metadata_[next_cell].Prepare(sequence, instrumentation, after_odd_hook, hook_context);
        published_active_.store(next_cell, std::memory_order_seq_cst);
        instrumentation.Step();
        producer_active_ = next_cell;
    }

    PortSequenceSnapshot Read(PortTestHook before_hazard_hook, void* hook_context,
                              std::uint64_t& validation_failures) const noexcept {
        for (;;) {
            const std::uint32_t cell = published_active_.load(std::memory_order_seq_cst);
            if (before_hazard_hook != nullptr) {
                before_hazard_hook(hook_context);
            }
            reader_hazard_.store(cell, std::memory_order_seq_cst);
            if (published_active_.load(std::memory_order_seq_cst) != cell) {
                reader_hazard_.store(kPortNoHazard, std::memory_order_seq_cst);
                ++validation_failures;
                continue;
            }
            const auto snapshot = metadata_[cell].ReadProtected();
            reader_hazard_.store(kPortNoHazard, std::memory_order_seq_cst);
            if (snapshot.has_value()) {
                return *snapshot;
            }
            ++validation_failures;
        }
    }

private:
    template <std::size_t Len, typename Message, typename Instrumentation>
    friend class PortTestAccess;

    std::array<PortSequenceMetadata, kPortCellCount> metadata_{};
    alignas(kPortCacheLineSize) mutable PortAtomicWord reader_hazard_{kPortNoHazard};
    alignas(kPortCacheLineSize) PortAtomicWord published_active_{0U};
    std::uint32_t producer_active_{0U};
};

}  // namespace detail

/**
 * @brief wait-free 写入、lock-free 读取的覆盖式 SPSC Port
 *
 * 每个逻辑槽位使用三个 payload/metadata 副本。消费者通过 32 位 hazard 保护正在
 * 读取的副本；生产者始终准备 inactive 且未被保护的第三副本，再以一个 32 位
 * active index 发布。head 使用相同结构。metadata 内的 64 位序号仍由单写者
 * 32 位 seqlock 发布，但 reader 从不等待 odd 的 inactive 副本。
 *
 * @tparam Len 逻辑容量，必须大于 2
 * @tparam Message 消息类型，必须可平凡复制
 * @tparam Instrumentation 显式测试插桩策略；生产默认策略不含钩子或计数器
 */
template <std::size_t Len, typename Message = PortFrame,
          typename Instrumentation = detail::PortNoInstrumentation>
class Port final : public BasePort, private Instrumentation {
    static_assert(Len > 2U, "Port capacity must be greater than 2");
    static_assert(std::is_trivially_copyable_v<Message>, "Port message must be trivially copyable");

public:
    using Callback = std::function<void(const Message&)>;

    /**
     * @brief 构造 Port
     * @param can_id Port 对应的 CAN ID；多 ID 邮箱使用 kAnyCanId
     * @param callback 发布完成后在生产者线程同步执行的可选回调
     */
    explicit Port(std::uint32_t can_id = BasePort::kAnyCanId, Callback callback = {})
        : BasePort(can_id), callback_(std::move(callback)) {}

    Port(const Port&) = delete;
    Port& operator=(const Port&) = delete;
    Port(Port&&) = delete;
    Port& operator=(Port&&) = delete;

    /**
     * @brief 发布一条消息
     * @param message 要发布的消息
     *
     * 无回调时本函数为 wait-free。ring 发布阶段没有循环、CAS、锁、等待或分配；
     * 用户回调耗时不属于 ring 的进度保证。回调不得递归 Push 同一个 Port。
     */
    void Push(const Message& message) {
        const std::uint64_t sequence = producer_sequence_ + 1U;
        Slot& slot = slots_[(sequence - 1U) % Len];

        const std::uint32_t hazard = slot.reader_hazard.load(std::memory_order_seq_cst);
        auto& instrumentation = InstrumentationState();
        instrumentation.Step();
        std::uint32_t next_cell = (slot.producer_active + 1U) % detail::kPortCellCount;
        if (next_cell == hazard) {
            next_cell = (next_cell + 1U) % detail::kPortCellCount;
        }

        ::new (static_cast<void*>(&slot.payloads[next_cell])) Message(message);
        slot.metadata[next_cell].Prepare(sequence, next_cell, instrumentation,
                                         instrumentation.AfterSlotOddHook(),
                                         instrumentation.AfterSlotOddContext());
        slot.published_active.store(next_cell, std::memory_order_seq_cst);
        instrumentation.Step();
        slot.producer_active = next_cell;
        if (instrumentation.AfterSlotActiveHook() != nullptr) {
            instrumentation.AfterSlotActiveHook()(instrumentation.AfterSlotActiveContext());
        }

        head_.Publish(sequence, instrumentation, instrumentation.AfterHeadOddHook(),
                      instrumentation.AfterHeadOddContext());
        producer_sequence_ = sequence;

        if (callback_) {
            callback_(message);
        }
    }

    /**
     * @brief 读取并消费最旧的保留消息
     * @return 有消息时返回完整消息，否则返回 nullopt
     */
    std::optional<Message> Pop() {
        for (;;) {
            const std::uint64_t head = ReadHead();
            if (consumer_sequence_ > head) {
                return std::nullopt;
            }

            const std::uint64_t oldest = head > Len ? head - Len + 1U : 1U;
            if (consumer_sequence_ < oldest) {
                consumer_sequence_ = oldest;
            }

            Slot& slot = slots_[(consumer_sequence_ - 1U) % Len];
            bool retry = false;
            std::uint64_t resync_sequence = consumer_sequence_;
            if (auto message = ReadTarget(slot, consumer_sequence_, retry, resync_sequence)) {
                ++consumer_sequence_;
                return message;
            }
            if (!retry) {
                ++validation_failure_count_;
                return std::nullopt;
            }
            if (resync_sequence > consumer_sequence_) {
                const std::uint64_t resync_oldest =
                    resync_sequence > Len ? resync_sequence - Len + 1U : 1U;
                if (consumer_sequence_ < resync_oldest) {
                    consumer_sequence_ = resync_oldest;
                }
            }
            ++validation_failure_count_;
        }
    }

    /**
     * @brief 查询消费者当前是否没有可读消息
     * @return 当前没有保留消息时返回 true
     *
     * 仅允许与 `Pop()`、`Clear()` 相同的单消费者调用。
     */
    bool Empty() const noexcept {
        return consumer_sequence_ > ReadHead();
    }

    /** @brief 丢弃调用点之前已经发布的所有未读消息 */
    void Clear() noexcept override {
        consumer_sequence_ = ReadHead() + 1U;
    }

private:
    using PayloadStorage = std::aligned_storage_t<sizeof(Message), alignof(Message)>;

    Instrumentation& InstrumentationState() noexcept {
        return static_cast<Instrumentation&>(*this);
    }

    const Instrumentation& InstrumentationState() const noexcept {
        return static_cast<const Instrumentation&>(*this);
    }

    struct Slot {
        std::array<PayloadStorage, detail::kPortCellCount> payloads{};
        std::array<detail::PortSlotMetadata, detail::kPortCellCount> metadata{};
        alignas(detail::kPortCacheLineSize) detail::PortAtomicWord reader_hazard{
            detail::kPortNoHazard};
        alignas(detail::kPortCacheLineSize) detail::PortAtomicWord published_active{0U};
        std::uint32_t producer_active{0U};
    };

    static Message* Payload(Slot& slot, std::uint32_t cell) noexcept {
        return std::launder(reinterpret_cast<Message*>(&slot.payloads[cell]));
    }

    std::uint64_t ReadHead() const noexcept {
        const auto& instrumentation = InstrumentationState();
        return head_
            .Read(instrumentation.BeforeHeadHazardHook(), instrumentation.BeforeHeadHazardContext(),
                  validation_failure_count_)
            .sequence;
    }

    std::optional<Message> ReadTarget(Slot& slot, std::uint64_t target, bool& retry,
                                      std::uint64_t& resync_sequence) noexcept {
        const std::uint32_t active = slot.published_active.load(std::memory_order_seq_cst);
        const auto& instrumentation = InstrumentationState();
        if (instrumentation.BeforeSlotHazardHook() != nullptr) {
            instrumentation.BeforeSlotHazardHook()(instrumentation.BeforeSlotHazardContext());
        }
        slot.reader_hazard.store(active, std::memory_order_seq_cst);
        if (instrumentation.AfterSlotHazardHook() != nullptr) {
            instrumentation.AfterSlotHazardHook()(instrumentation.AfterSlotHazardContext());
        }
        if (slot.published_active.load(std::memory_order_seq_cst) != active) {
            slot.reader_hazard.store(detail::kPortNoHazard, std::memory_order_seq_cst);
            retry = true;
            return std::nullopt;
        }
        const auto snapshot = slot.metadata[active].ReadProtected();
        if (!snapshot.has_value() || snapshot->cell != active) {
            slot.reader_hazard.store(detail::kPortNoHazard, std::memory_order_seq_cst);
            // 元数据发布协议被并发写入打断时继续重试，避免把暂时不一致误报为空邮箱。
            retry = true;
            return std::nullopt;
        }
        if (snapshot->sequence != target) {
            slot.reader_hazard.store(detail::kPortNoHazard, std::memory_order_seq_cst);
            retry = snapshot->sequence > target;
            resync_sequence = snapshot->sequence;
            return std::nullopt;
        }
        const Message result = *Payload(slot, active);
        slot.reader_hazard.store(detail::kPortNoHazard, std::memory_order_seq_cst);
        retry = false;
        return result;
    }

    friend class detail::PortTestAccess<Len, Message, Instrumentation>;

    std::array<Slot, Len> slots_{};
    detail::PortHeadPublication head_;
    alignas(detail::kPortCacheLineSize) std::uint64_t producer_sequence_{0U};
    Callback callback_;
    alignas(detail::kPortCacheLineSize) std::uint64_t consumer_sequence_{1U};
    mutable std::uint64_t validation_failure_count_{0U};
};

namespace detail {

template <std::size_t Len, typename Message, typename Instrumentation>
class PortTestAccess {
public:
    static constexpr std::uint64_t kSlotPublicationAtomicSteps = 7U;
    static constexpr std::uint64_t kWriterAtomicSteps = 13U;

    using TestPort = Port<Len, Message, Instrumentation>;

    static void SetAfterHazardHook(TestPort& port, PortTestHook hook, void* context) noexcept {
        auto& instrumentation = port.InstrumentationState();
        instrumentation.after_slot_hazard_hook = hook;
        instrumentation.after_slot_hazard_context = context;
    }

    static void SetAfterSlotOddStoreHook(TestPort& port, PortTestHook hook,
                                         void* context) noexcept {
        auto& instrumentation = port.InstrumentationState();
        instrumentation.after_slot_odd_hook = hook;
        instrumentation.after_slot_odd_context = context;
    }

    static void SetAfterSlotActiveStoreHook(TestPort& port, PortTestHook hook,
                                            void* context) noexcept {
        auto& instrumentation = port.InstrumentationState();
        instrumentation.after_slot_active_hook = hook;
        instrumentation.after_slot_active_context = context;
    }

    static void SetAfterHeadOddStoreHook(TestPort& port, PortTestHook hook,
                                         void* context) noexcept {
        auto& instrumentation = port.InstrumentationState();
        instrumentation.after_head_odd_hook = hook;
        instrumentation.after_head_odd_context = context;
    }

    static void SetBeforeSlotHazardHook(TestPort& port, PortTestHook hook, void* context) noexcept {
        auto& instrumentation = port.InstrumentationState();
        instrumentation.before_slot_hazard_hook = hook;
        instrumentation.before_slot_hazard_context = context;
    }

    static void SetBeforeHeadHazardHook(TestPort& port, PortTestHook hook, void* context) noexcept {
        auto& instrumentation = port.InstrumentationState();
        instrumentation.before_head_hazard_hook = hook;
        instrumentation.before_head_hazard_context = context;
    }

    static void ForcePublicationVersionsNearWrap(TestPort& port) noexcept {
        for (auto& slot : port.slots_) {
            for (auto& metadata : slot.metadata) {
                metadata.ForceVersionNearWrap();
            }
        }
        for (auto& metadata : port.head_.metadata_) {
            metadata.ForceVersionNearWrap();
        }
    }

    static void ResetEmptySequence(TestPort& port, std::uint64_t published_sequence) noexcept {
        port.producer_sequence_ = published_sequence;
        port.consumer_sequence_ = published_sequence + 1U;
        port.head_.Publish(published_sequence, port.InstrumentationState(), nullptr, nullptr);
    }

    static std::uint64_t WriterStepCount(const TestPort& port) noexcept {
        return port.InstrumentationState().atomic_steps;
    }

    static std::uint64_t ValidationFailureCount(const TestPort& port) noexcept {
        return port.validation_failure_count_;
    }

    static std::uintptr_t SlotReaderHazardAddress(const TestPort& port, std::size_t slot) noexcept {
        return reinterpret_cast<std::uintptr_t>(&port.slots_[slot].reader_hazard);
    }

    static std::uintptr_t SlotPublishedActiveAddress(const TestPort& port,
                                                     std::size_t slot) noexcept {
        return reinterpret_cast<std::uintptr_t>(&port.slots_[slot].published_active);
    }

    static std::uintptr_t HeadReaderHazardAddress(const TestPort& port) noexcept {
        return reinterpret_cast<std::uintptr_t>(&port.head_.reader_hazard_);
    }

    static std::uintptr_t HeadPublishedActiveAddress(const TestPort& port) noexcept {
        return reinterpret_cast<std::uintptr_t>(&port.head_.published_active_);
    }

    static std::uintptr_t ProducerSequenceAddress(const TestPort& port) noexcept {
        return reinterpret_cast<std::uintptr_t>(&port.producer_sequence_);
    }

    static std::uintptr_t ConsumerSequenceAddress(const TestPort& port) noexcept {
        return reinterpret_cast<std::uintptr_t>(&port.consumer_sequence_);
    }
};

}  // namespace detail

}  // namespace encos
