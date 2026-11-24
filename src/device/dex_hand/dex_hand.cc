// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "device/dex_hand/dex_hand.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <stdexcept>
#include <utility>

#include "encos/driver_manager.h"
#include "platform/sync.h"
#include "platform/wait.h"
#include "utils/scope_exit.h"

namespace encos {
namespace {

constexpr uint8_t kQuery = 0xF0;
constexpr uint8_t kConfigure = 0xF8;
constexpr uint8_t kAckBit = 0x04;
constexpr uint8_t kSamplePoints = 0x00;
constexpr uint8_t kBaseId = 0x01;
constexpr uint8_t kMaxBaseId = 247;

MotorPackMsg MakeRequest(uint8_t command, uint8_t object, uint8_t length) {
    MotorPackMsg request{};
    request.len = length;
    request.data[0] = command;
    request.data[1] = object;
    return request;
}

bool IsValidBaseId(int id) {
    return id >= 1 && id <= kMaxBaseId;
}

uint16_t ReadLe16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0] | (static_cast<unsigned>(bytes[1]) << 8U));
}

void WriteLe16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8U);
}

DexHandReply<DexHandSamplePoints> DecodeSamplePoints(const DexHandReply<MotorPackMsg>& reply) {
    if (!reply.value) {
        return {reply.status, std::nullopt};
    }
    const auto* bytes = reply.value->data;
    return {reply.status, DexHandSamplePoints{ReadLe16(bytes + 3), ReadLe16(bytes + 5)}};
}

DexHandReply<uint8_t> DecodeBaseId(const DexHandReply<MotorPackMsg>& reply) {
    if (!reply.value) {
        return {reply.status, std::nullopt};
    }
    return {reply.status, reply.value->data[3]};
}

}  // namespace

struct DexHand::Impl {
    Impl(uint32_t id, std::function<void(const MotorPackMsg&)> send)
        : request_id(id), writer(std::move(send)) {}

    struct PendingRequest {
        const MotorPackMsg& request;
        std::optional<MotorPackMsg> reply;
        std::atomic<bool> ready{false};
    };

    const uint32_t request_id;
    const std::function<void(const MotorPackMsg&)> writer;
    platform::Mutex mutex;
    std::condition_variable_any condition;
    PendingRequest* pending = nullptr;
    bool retiring = false;
};

DexHand::DexHand(uint32_t request_id, std::function<void(const MotorPackMsg&)> writer)
    : impl_(std::make_unique<Impl>(request_id, std::move(writer))) {}

DexHand::~DexHand() = default;

DexHandReply<DexHandSamplePoints> DexHand::GetSamplePoints(DexHandRequestOptions options) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    return DecodeSamplePoints(Exchange(MakeRequest(kQuery, kSamplePoints, 2), options));
}

DexHandReply<DexHandSamplePoints> DexHand::SetSamplePoints(DexHandSamplePoints points,
                                                           DexHandRequestOptions options) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    auto request = MakeRequest(kConfigure, kSamplePoints, 6);
    WriteLe16(request.data + 2, points.nominal);
    WriteLe16(request.data + 4, points.data);
    return DecodeSamplePoints(Exchange(request, options));
}

DexHandReply<uint8_t> DexHand::GetBaseId(DexHandRequestOptions options) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    return DecodeBaseId(Exchange(MakeRequest(kQuery, kBaseId, 2), options));
}

DexHandReply<uint8_t> DexHand::SetBaseId(int new_base_id, DexHandRequestOptions options) {
    auto operation = EncosDriverManager::Instance().AcquireDeviceOperation(this);
    if (!IsValidBaseId(new_base_id)) {
        throw std::invalid_argument("DexHand base ID must be in [1, 247]");
    }
    auto request = MakeRequest(kConfigure, kBaseId, 3);
    request.data[2] = static_cast<uint8_t>(new_base_id);
    return DecodeBaseId(Exchange(request, options));
}

DexHandReply<MotorPackMsg> DexHand::Exchange(MotorPackMsg request, DexHandRequestOptions options) {
    request.id = impl_->request_id;
    request.frame_flags = options.canfd ? kCanFrameFlagFdMask : 0;
    Impl::PendingRequest pending{request, std::nullopt};

    platform::UniqueLock<platform::Mutex> lock(impl_->mutex);
    if (impl_->retiring) {
        return {DexHandStatus::Canceled, std::nullopt};
    }
    if (impl_->pending) {
        return {DexHandStatus::RequestInProgress, std::nullopt};
    }
    impl_->pending = &pending;
    const auto clear_pending = utils::MakeScopeExit([&]() {
        if (!lock.owns_lock()) {
            lock.lock();
        }
        impl_->pending = nullptr;
    });

    lock.unlock();
    impl_->writer(request);
    lock.lock();
    if (!platform::WaitForPredicate(impl_->condition, lock, options.timeout, [&pending]() {
            return pending.ready.load(std::memory_order_acquire);
        })) {
        return {DexHandStatus::Timeout, std::nullopt};
    }
    if (!pending.reply) {
        return {DexHandStatus::Canceled, std::nullopt};
    }
    const auto status = static_cast<DexHandStatus>(pending.reply->data[2]);
    return {status, status == DexHandStatus::Success ? pending.reply : std::nullopt};
}

bool DexHand::IsReplyFrame(const MotorPackMsg& message) noexcept {
    if ((message.frame_flags != 0 && message.frame_flags != kCanFrameFlagFdMask) ||
        message.len < 3 ||
        (message.data[0] != (kQuery | kAckBit) && message.data[0] != (kConfigure | kAckBit)) ||
        message.data[1] > kBaseId ||
        message.data[2] > static_cast<uint8_t>(DexHandStatus::FirmwareBusy)) {
        return false;
    }
    if (message.data[2] != 0) {
        return message.len == 3;
    }
    if (message.data[1] == kSamplePoints) {
        return message.len == 7;
    }
    return message.len == 4 && IsValidBaseId(message.data[3]);
}

void DexHand::OnMessage(const MotorPackMsg& message) {
    platform::LockGuard<platform::Mutex> lock(impl_->mutex);
    auto* pending = impl_->pending;
    if (!pending || pending->ready.load(std::memory_order_relaxed)) {
        return;
    }
    const auto& request = pending->request;
    if (message.id != request.id || message.frame_flags != request.frame_flags ||
        message.data[0] != (request.data[0] | kAckBit) || message.data[1] != request.data[1]) {
        return;
    }
    if (request.data[0] == kConfigure && message.data[2] == 0 &&
        !std::equal(request.data + 2, request.data + request.len, message.data + 3)) {
        return;
    }
    pending->reply = message;
    pending->ready.store(true, std::memory_order_release);
    impl_->condition.notify_one();
}

void DexHand::CancelWaiters() {
    platform::LockGuard<platform::Mutex> lock(impl_->mutex);
    impl_->retiring = true;
    if (impl_->pending) {
        impl_->pending->ready.store(true, std::memory_order_release);
        impl_->condition.notify_one();
    }
}

}  // namespace encos
