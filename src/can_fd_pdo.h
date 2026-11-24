// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include "example_igh/ethercat_layer.h"

enum {
    EC_CAN_SLOT_SIZE = 14,
    EC_CAN_SLOT_PAYLOAD_SIZE = 8,
};

bool ec_is_can_fd_layout(const EcSlavePdoLayout* layout);
bool ec_can_fd_frame_is_valid(const EcCanFrame* frame);
size_t ec_can_slots_for_length(size_t len);
void ec_can_fd_write_frame(uint8_t* first_slot, const EcCanFrame* frame);
bool ec_can_fd_read_slot(const uint8_t* slot, bool legacy_short_slots, EcCanFrame* frame);
void ec_can_fd_begin_input(EcCanFdGroup groups[EC_CAN_FD_GROUPS_PER_BUS], uint64_t now_ns);
bool ec_can_fd_receive_slot(const uint8_t* slot, bool legacy_short_slots,
                            EcCanFdGroup groups[EC_CAN_FD_GROUPS_PER_BUS], uint64_t deadline_ns,
                            EcCanFrame* frame);
