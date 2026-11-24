// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

#include "device/motor/types.h"
#include "utils/emr2/relay_frame.h"

namespace encos {

struct RelayQueuedMessage {
    MotorMessage message;
    std::size_t generation = 0;
};

inline std::vector<uint8_t> EncodeQueuedRelayFrames(
    RelayFrameType type, const std::vector<RelayQueuedMessage>& messages) {
    if (messages.empty()) {
        return EncodeRelayFrames(type, {});
    }

    std::vector<uint8_t> result;
    std::vector<MotorMessage> frame_messages;
    std::unordered_map<int, std::size_t> frame_generations;
    const auto flush = [&]() {
        if (frame_messages.empty()) {
            return;
        }
        auto encoded = EncodeRelayFrames(type, frame_messages);
        result.insert(result.end(), std::make_move_iterator(encoded.begin()),
                      std::make_move_iterator(encoded.end()));
        frame_messages.clear();
        frame_generations.clear();
    };

    for (const auto& queued : messages) {
        const auto found = frame_generations.find(queued.message.bus_idx);
        if (frame_messages.size() == 255U ||
            (found != frame_generations.end() && found->second != queued.generation)) {
            flush();
        }
        frame_generations.emplace(queued.message.bus_idx, queued.generation);
        frame_messages.push_back(queued.message);
    }
    flush();
    return result;
}

/** @brief 保留总线代际边界，并按载荷预算输出独立 WebSocket 包 */
inline std::vector<std::vector<uint8_t>> EncodeQueuedRelayPackets(
    RelayFrameType type, const std::vector<RelayQueuedMessage>& messages,
    RelayWireOptions& options) {
    auto next = options;
    std::vector<std::vector<uint8_t>> result;
    MotorMessages group;
    std::unordered_map<int, std::size_t> generations;
    const auto flush = [&]() {
        auto packets = EncodeRelayPackets(type, group, next);
        result.insert(result.end(), std::make_move_iterator(packets.begin()),
                      std::make_move_iterator(packets.end()));
        group.clear();
        generations.clear();
    };
    for (const auto& queued : messages) {
        const auto found = generations.find(queued.message.bus_idx);
        if (found != generations.end() && found->second != queued.generation) {
            flush();
        }
        generations.emplace(queued.message.bus_idx, queued.generation);
        group.push_back(queued.message);
    }
    if (!group.empty() || messages.empty()) {
        flush();
    }
    ++next.batch_id;
    options = next;
    return result;
}

inline void TrimRelayMessagesToNewest(std::vector<MotorMessage>& messages, std::size_t capacity) {
    if (messages.size() <= capacity) {
        return;
    }
    messages.erase(messages.begin(), messages.begin() + (messages.size() - capacity));
}

inline void TrimRelayMessagesToNewest(std::vector<RelayQueuedMessage>& messages,
                                      std::size_t capacity) {
    if (messages.size() <= capacity) {
        return;
    }
    messages.erase(messages.begin(), messages.begin() + (messages.size() - capacity));
}

inline void ReinsertFailedRelayMessages(std::vector<MotorMessage>& current,
                                        std::vector<MotorMessage> failed, std::size_t capacity) {
    failed.insert(failed.end(), std::make_move_iterator(current.begin()),
                  std::make_move_iterator(current.end()));
    current = std::move(failed);
    TrimRelayMessagesToNewest(current, capacity);
}

inline void ReinsertFailedRelayMessages(std::vector<RelayQueuedMessage>& current,
                                        std::vector<RelayQueuedMessage> failed,
                                        std::size_t capacity) {
    failed.insert(failed.end(), std::make_move_iterator(current.begin()),
                  std::make_move_iterator(current.end()));
    current = std::move(failed);
    TrimRelayMessagesToNewest(current, capacity);
}

}  // namespace encos
