#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "platform/sync.h"

namespace encos {

/**
 * @brief 跟踪一个托管对象已进入的公开操作并协调退役排空
 *
 * `TryEnter` 只在对象尚未退役时增加活动计数；这个计数就是对象存续引用。
 * 成功进入后，即使 Registry 的 snapshot hazard 已释放，删除路径也不得回收
 * `OperationGate` 或领域对象，直到对应的 `Leave` 将计数减为零。`Retire` 以同一个
 * 原子字设置退役位，使“接纳新操作”和“拒绝后续操作”具有唯一线性化顺序。
 *
 * 删除路径必须先在管理器索引锁内完成 `Retire`，随后释放管理器索引锁和 Registry
 * writer 锁，再等待路由在途回调与 `WaitForDrain`。`wait_mutex_` 只允许独立获取，
 * 不得与管理器对象/路由锁、Registry writer 锁或设备对象锁嵌套，避免退出操作通知
 * 排空时形成反向锁序。
 */
class OperationGate {
public:
    bool TryEnter() noexcept {
        auto state = state_.load(std::memory_order_acquire);
        while ((state & kRetiring) == 0) {
            if ((state & kActiveMask) == kActiveMask) {
                return false;
            }
            if (state_.compare_exchange_weak(state, state + 1, std::memory_order_acquire,
                                             std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    void Leave() noexcept {
        const auto previous = state_.fetch_sub(1, std::memory_order_release);
        if ((previous & kActiveMask) == 0) {
            std::terminate();
        }
        if ((previous & kRetiring) != 0 && (previous & kActiveMask) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            platform::LockGuard<platform::Mutex> lock(wait_mutex_);
            drained_.notify_all();
        }
    }

    void Retire() noexcept {
        state_.fetch_or(kRetiring, std::memory_order_acq_rel);
    }

    bool HasActiveOperations() const noexcept {
        return (state_.load(std::memory_order_acquire) & kActiveMask) != 0;
    }

    void WaitForDrain() {
        platform::UniqueLock<platform::Mutex> lock(wait_mutex_);
        drained_.wait(lock, [this] {
            return (state_.load(std::memory_order_acquire) & kActiveMask) == 0;
        });
    }

private:
    static constexpr std::uint32_t kRetiring = std::uint32_t{1} << 31u;
    static constexpr std::uint32_t kActiveMask = kRetiring - 1;

    std::atomic<std::uint32_t> state_{0};
    platform::Mutex wait_mutex_;
    std::condition_variable_any drained_;
};

enum class OperationKind : std::uint8_t {
    Adapter,
    Bus,
    Motor,
    Battery,
    Imu,
    Pms,
    Glove,
    GloveEncoder,
    GloveCalibrator,
};

/** @brief OperationRegistry 进入失败的精确原因 */
enum class OperationEnterFailure : std::uint8_t {
    None,
    NotRegisteredOrRetiring,
    HazardCapacityExhausted,
};

/** @brief OperationRegistry 查找和进入操作的结果 */
struct OperationEnterResult {
    OperationGate* gate;
    OperationEnterFailure failure;
};

/**
 * @brief 使用不可变快照和 hazard slot 提供无管理器互斥锁的操作入口查找
 *
 * 写路径在管理器慢路径锁下发布新快照；读路径用每线程 hazard slot 固定当前快照。
 * `MaxHazardThreads` 是同时参与该模板实例查找的线程上限，生产别名使用 4096，
 * 小容量实例仅用于确定性测试容量耗尽诊断。
 *
 * 生命周期不变量如下：
 *
 * - snapshot 只保存 `entries_` 中稳定 `Entry` 的非拥有指针；读者发布 hazard 并
 *   二次确认 snapshot 后，writer 才能安全地让它执行有界查找。
 * - `TryEnter` 在清除 snapshot hazard 前增加 Gate 活动计数，因此活动计数接替 hazard
 *   成为 Entry 和领域对象的存续引用；未执行对应 `Leave` 时禁止回收。
 * - 退役先把 Entry 标为未注册并设置 Gate 退役位，再等待活动计数归零。回收随后发布
 *   不含该 Entry 的新 snapshot，等待所有旧 snapshot hazard 清除，最后才删除旧
 *   snapshot 和 Entry；同一地址的新对象因而属于新的 Gate 世代。
 *
 * 锁序固定为管理器对象/路由锁在外、`writer_mutex_` 在内。Registry 不回调管理器；
 * `WaitForReaders` 只观察 hazard 且不获取其它锁。任何可能阻塞于 route 或 Gate 排空
 * 的等待都必须在释放管理器锁和 `writer_mutex_` 后执行。
 */
template <std::size_t MaxHazardThreads>
class BasicOperationRegistry {
public:
    BasicOperationRegistry() {
        current_.store(new Snapshot({}), std::memory_order_seq_cst);
    }

    ~BasicOperationRegistry() {
        auto* snapshot = current_.exchange(nullptr, std::memory_order_seq_cst);
        WaitForReaders(snapshot);
        delete snapshot;
    }

    BasicOperationRegistry(const BasicOperationRegistry&) = delete;
    BasicOperationRegistry& operator=(const BasicOperationRegistry&) = delete;

    OperationGate* Register(void* object, OperationKind kind) {
        platform::LockGuard<platform::Mutex> lock(writer_mutex_);
        for (const auto& entry : entries_) {
            if (entry->object == object && entry->kind == kind &&
                entry->registered.load(std::memory_order_acquire)) {
                throw std::logic_error("Operation gate already registered");
            }
        }
        auto entry = std::make_unique<Entry>(object, kind);
        auto* result = &entry->gate;
        entries_.push_back(std::move(entry));
        try {
            PublishSnapshot();
        } catch (...) {
            entries_.pop_back();
            throw;
        }
        EraseReclaimableEntries();
        return result;
    }

    OperationGate* TryEnter(void* object, OperationKind kind) const noexcept {
        return TryEnterDetailed(object, kind).gate;
    }

    OperationEnterResult TryEnterDetailed(void* object, OperationKind kind) const noexcept {
        auto* hazard = ThreadHazard();
        if (hazard == nullptr) {
            return {nullptr, OperationEnterFailure::HazardCapacityExhausted};
        }
        auto* snapshot = ProtectSnapshot(hazard);
        auto* entry = snapshot == nullptr ? nullptr : snapshot->Find(object, kind);
        OperationGate* gate = nullptr;
        if (entry != nullptr && entry->registered.load(std::memory_order_acquire) &&
            entry->gate.TryEnter()) {
            gate = &entry->gate;
        }
        ClearHazard(hazard);
        return {gate, gate == nullptr ? OperationEnterFailure::NotRegisteredOrRetiring
                                      : OperationEnterFailure::None};
    }

    bool HasActiveOperations(void* object, OperationKind kind) noexcept {
        platform::LockGuard<platform::Mutex> lock(writer_mutex_);
        for (auto iterator = entries_.rbegin(); iterator != entries_.rend(); ++iterator) {
            const auto& entry = *iterator;
            if (entry->object == object && entry->kind == kind &&
                entry->registered.load(std::memory_order_acquire)) {
                return entry->gate.HasActiveOperations();
            }
        }
        return false;
    }

    OperationGate* Retire(void* object, OperationKind kind) noexcept {
        platform::LockGuard<platform::Mutex> lock(writer_mutex_);
        for (auto iterator = entries_.rbegin(); iterator != entries_.rend(); ++iterator) {
            auto& entry = *iterator;
            if (entry->object == object && entry->kind == kind) {
                if (entry->registered.exchange(false, std::memory_order_acq_rel)) {
                    entry->gate.Retire();
                }
                return &entry->gate;
            }
        }
        return nullptr;
    }

    void ReclaimRetired(void* object, OperationKind kind) noexcept {
        platform::LockGuard<platform::Mutex> lock(writer_mutex_);
        try {
            for (auto& entry : entries_) {
                if (entry->object == object && entry->kind == kind &&
                    !entry->registered.load(std::memory_order_acquire) &&
                    !entry->gate.HasActiveOperations()) {
                    entry->reclaimable = true;
                }
            }
            PublishSnapshot();
            EraseReclaimableEntries();
        } catch (...) {}
    }

    std::size_t EntryCountForTests() const noexcept {
        return entries_.size();
    }

    std::size_t MaxProbeForTests() const noexcept {
        auto* hazard = ThreadHazard();
        if (hazard == nullptr) {
            return 0;
        }
        auto* snapshot = ProtectSnapshot(hazard);
        const auto max_probe = snapshot == nullptr ? 0 : snapshot->max_probe;
        ClearHazard(hazard);
        return max_probe;
    }

private:
    struct Entry {
        Entry(void* registered_object, OperationKind registered_kind)
            : object(registered_object), kind(registered_kind) {}

        void* const object;
        const OperationKind kind;
        OperationGate gate;
        std::atomic<bool> registered{true};
        bool reclaimable = false;
    };

    struct Snapshot {
        explicit Snapshot(const std::vector<std::unique_ptr<Entry>>& entries) {
            std::size_t active = 0;
            for (const auto& entry : entries) {
                if (entry->registered.load(std::memory_order_acquire)) {
                    ++active;
                }
            }
            std::size_t capacity = 8;
            while (capacity < active * 2) {
                capacity *= 2;
            }
            slots.assign(capacity, nullptr);
            for (const auto& entry : entries) {
                if (!entry->registered.load(std::memory_order_acquire)) {
                    continue;
                }
                const auto start = Hash(entry->object, entry->kind) & (capacity - 1);
                std::size_t probe = 0;
                while (slots[(start + probe) & (capacity - 1)] != nullptr) {
                    ++probe;
                }
                slots[(start + probe) & (capacity - 1)] = entry.get();
                max_probe = std::max(max_probe, probe);
            }
        }

        Entry* Find(void* object, OperationKind kind) const noexcept {
            const auto start = Hash(object, kind) & (slots.size() - 1);
            for (std::size_t probe = 0; probe <= max_probe; ++probe) {
                auto* entry = slots[(start + probe) & (slots.size() - 1)];
                if (entry == nullptr) {
                    return nullptr;
                }
                if (entry->object == object && entry->kind == kind) {
                    return entry;
                }
            }
            return nullptr;
        }

        static std::size_t Hash(const void* object, OperationKind kind) noexcept {
            auto value = reinterpret_cast<std::uintptr_t>(object) >> 4u;
            value ^= static_cast<std::uintptr_t>(kind) * std::uintptr_t{0x9E3779B9u};
            value ^= value >> 16u;
            return static_cast<std::size_t>(value);
        }

        std::vector<Entry*> slots;
        std::size_t max_probe = 0;
    };

    struct alignas(64) HazardSlot {
        HazardSlot() noexcept : claimed(false), snapshot(nullptr) {}

        std::atomic<bool> claimed;
        std::atomic<const Snapshot*> snapshot;
    };

    struct HazardOwner {
        HazardOwner() {
            for (auto& candidate : hazard_slots_) {
                bool expected = false;
                if (candidate.claimed.compare_exchange_strong(expected, true,
                                                              std::memory_order_acq_rel)) {
                    slot = &candidate;
                    return;
                }
            }
        }

        ~HazardOwner() {
            if (slot != nullptr) {
                slot->snapshot.store(nullptr, std::memory_order_seq_cst);
                slot->claimed.store(false, std::memory_order_release);
            }
        }

        HazardSlot* slot = nullptr;
    };

    static HazardSlot* ThreadHazard() noexcept {
        thread_local HazardOwner owner;
        return owner.slot;
    }

    Snapshot* ProtectSnapshot(HazardSlot* hazard) const noexcept {
        Snapshot* snapshot = nullptr;
        do {
            snapshot = current_.load(std::memory_order_seq_cst);
            hazard->snapshot.store(snapshot, std::memory_order_seq_cst);
        } while (snapshot != current_.load(std::memory_order_seq_cst));
        return snapshot;
    }

    static void ClearHazard(HazardSlot* hazard) noexcept {
        hazard->snapshot.store(nullptr, std::memory_order_seq_cst);
    }

    static void WaitForReaders(const Snapshot* snapshot) noexcept {
        if (snapshot == nullptr) {
            return;
        }
        for (const auto& hazard : hazard_slots_) {
            while (hazard.snapshot.load(std::memory_order_seq_cst) == snapshot) {
                std::this_thread::yield();
            }
        }
    }

    void PublishSnapshot() {
        auto replacement = std::make_unique<Snapshot>(entries_);
        auto* previous = current_.exchange(replacement.release(), std::memory_order_seq_cst);
        WaitForReaders(previous);
        delete previous;
    }

    void EraseReclaimableEntries() {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [](const auto& entry) {
                                          return entry->reclaimable;
                                      }),
                       entries_.end());
    }

    inline static std::array<HazardSlot, MaxHazardThreads> hazard_slots_{};
    platform::Mutex writer_mutex_;
    std::atomic<Snapshot*> current_{nullptr};
    std::vector<std::unique_ptr<Entry>> entries_;
};

using OperationRegistry = BasicOperationRegistry<4096>;

}  // namespace encos
