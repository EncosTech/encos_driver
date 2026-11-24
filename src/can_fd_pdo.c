// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "can_fd_pdo.h"

#include <string.h>

enum {
    EC_CAN_FLAGS_OFFSET = 4,
    EC_CAN_LENGTH_OFFSET = 5,
    EC_CAN_DATA_OFFSET = 6,
    EC_CAN_FRAGMENT_FLAG = 0x10,
    EC_CAN_FRAGMENT_INDEX_SHIFT = 5,
};

bool ec_is_can_fd_layout(const EcSlavePdoLayout* layout) {
    return layout->format == EC_SLAVE_FORMAT_CAN_FD_3_BUS ||
           layout->format == EC_SLAVE_FORMAT_CAN_FD_3_BUS_10_SLOTS ||
           layout->format == EC_SLAVE_FORMAT_CAN_FD_8_BUS ||
           layout->format == EC_SLAVE_FORMAT_CAN_FD_8_BUS_10_SLOTS;
}

bool ec_can_fd_frame_is_valid(const EcCanFrame* frame) {
    const bool fd = (frame->frame_flags & MOTOR_CAN_FLAG_FD_BIT1) != 0;
    const uint32_t max_id = (frame->frame_flags & MOTOR_CAN_FLAG_EFF) ? 0x1FFFFFFF : 0x7FF;
    return frame->id != 0 && frame->id <= max_id && frame->len != 0 &&
           frame->len <= (fd ? sizeof(frame->data) : EC_CAN_SLOT_PAYLOAD_SIZE) &&
           frame->frame_flags == motor_sanitize_flags(frame->frame_flags) &&
           (fd ? !(frame->frame_flags & MOTOR_CAN_FLAG_RTR)
               : !(frame->frame_flags & MOTOR_CAN_FLAG_FD_BIT2));
}

size_t ec_can_slots_for_length(size_t len) {
    return (len + EC_CAN_SLOT_PAYLOAD_SIZE - 1) / EC_CAN_SLOT_PAYLOAD_SIZE;
}

static size_t fragment_length(size_t remaining) {
    return remaining < EC_CAN_SLOT_PAYLOAD_SIZE ? remaining : EC_CAN_SLOT_PAYLOAD_SIZE;
}

void ec_can_fd_write_frame(uint8_t* first_slot, const EcCanFrame* frame) {
    const size_t count = ec_can_slots_for_length(frame->len);
    for (size_t index = 0; index < count; ++index) {
        uint8_t* slot = first_slot + index * EC_CAN_SLOT_SIZE;
        memset(slot, 0, EC_CAN_SLOT_SIZE);
        slot[0] = (uint8_t) frame->id;
        slot[1] = (uint8_t) (frame->id >> 8);
        slot[2] = (uint8_t) (frame->id >> 16);
        slot[3] = (uint8_t) (frame->id >> 24);
        slot[EC_CAN_FLAGS_OFFSET] = frame->frame_flags;
        if (count > 1) {
            slot[EC_CAN_FLAGS_OFFSET] |=
                (uint8_t) (EC_CAN_FRAGMENT_FLAG | (index << EC_CAN_FRAGMENT_INDEX_SHIFT));
        }
        slot[EC_CAN_LENGTH_OFFSET] = frame->len;
        if (!(frame->frame_flags & MOTOR_CAN_FLAG_RTR)) {
            const size_t offset = index * EC_CAN_SLOT_PAYLOAD_SIZE;
            memcpy(slot + EC_CAN_DATA_OFFSET, frame->data + offset,
                   fragment_length(frame->len - offset));
        }
    }
}

bool ec_can_fd_read_slot(const uint8_t* slot, bool legacy_short_slots, EcCanFrame* frame) {
    *frame = (EcCanFrame){
        .id = (uint32_t) slot[0] | ((uint32_t) slot[1] << 8) | ((uint32_t) slot[2] << 16) |
              ((uint32_t) slot[3] << 24),
        .frame_flags = motor_sanitize_flags(slot[EC_CAN_FLAGS_OFFSET]),
        .len = slot[EC_CAN_LENGTH_OFFSET],
    };
    if (frame->len == 0 || ((!legacy_short_slots || frame->len > EC_CAN_SLOT_PAYLOAD_SIZE) &&
                            !ec_can_fd_frame_is_valid(frame))) {
        return false;
    }
    if (frame->len > EC_CAN_SLOT_PAYLOAD_SIZE) {
        return (slot[EC_CAN_FLAGS_OFFSET] & EC_CAN_FRAGMENT_FLAG) != 0;
    }
    if (slot[EC_CAN_FLAGS_OFFSET] != frame->frame_flags) {
        return false;
    }
    if (!(frame->frame_flags & MOTOR_CAN_FLAG_RTR)) {
        memcpy(frame->data, slot + EC_CAN_DATA_OFFSET, frame->len);
    }
    return true;
}

void ec_can_fd_begin_input(EcCanFdGroup groups[EC_CAN_FD_GROUPS_PER_BUS], uint64_t now_ns) {
    for (size_t i = 0; i < EC_CAN_FD_GROUPS_PER_BUS; ++i) {
        EcCanFdGroup* group = &groups[i];
        if (group->from_previous_pdo || now_ns >= group->deadline_ns) {
            group->received = 0;
        }
        group->from_previous_pdo = group->received != 0;
    }
}

bool ec_can_fd_receive_slot(const uint8_t* slot, bool legacy_short_slots,
                            EcCanFdGroup groups[EC_CAN_FD_GROUPS_PER_BUS], uint64_t deadline_ns,
                            EcCanFrame* frame) {
    if (!ec_can_fd_read_slot(slot, legacy_short_slots, frame)) {
        return false;
    }
    if (frame->len <= EC_CAN_SLOT_PAYLOAD_SIZE) {
        return true;
    }

    EcCanFdGroup* matching = NULL;
    EcCanFdGroup* free_group = NULL;
    for (size_t i = 0; i < EC_CAN_FD_GROUPS_PER_BUS; ++i) {
        EcCanFdGroup* group = &groups[i];
        if (group->received == 0) {
            if (free_group == NULL) {
                free_group = group;
            }
        } else if (group->frame.id == frame->id && group->frame.frame_flags == frame->frame_flags &&
                   group->frame.len == frame->len) {
            matching = group;
            break;
        }
    }

    const size_t count = ec_can_slots_for_length(frame->len);
    const unsigned int index = slot[EC_CAN_FLAGS_OFFSET] >> EC_CAN_FRAGMENT_INDEX_SHIFT;
    if (index >= count) {
        if (matching != NULL) {
            matching->received = 0;
        }
        return false;
    }
    EcCanFdGroup* group = matching;
    if (index == 0) {
        group = matching != NULL ? matching : free_group;
        if (group != NULL) {
            *group = (EcCanFdGroup){.frame = *frame, .deadline_ns = deadline_ns};
        }
    }
    if (group == NULL) {
        return false;
    }

    const size_t offset = index * EC_CAN_SLOT_PAYLOAD_SIZE;
    const size_t len = fragment_length(frame->len - offset);
    uint8_t* target = group->frame.data + offset;
    const uint8_t* source = slot + EC_CAN_DATA_OFFSET;
    const unsigned int bit = 1U << index;
    if ((group->received & bit) != 0) {
        if (memcmp(target, source, len) != 0) {
            group->received = 0;
        }
        return false;
    }
    memcpy(target, source, len);
    group->received |= bit;
    if (group->received != (1U << count) - 1) {
        return false;
    }
    *frame = group->frame;
    group->received = 0;
    return true;
}
