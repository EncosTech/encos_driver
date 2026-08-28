#pragma once

#include <cstddef>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

#include "motor/types.h"
#include "relay/relay_frame.h"

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
