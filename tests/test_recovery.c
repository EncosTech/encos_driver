// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "example_igh/ethercat_layer.h"

#define CHECK(expr)                                       \
    do {                                                  \
        if (!(expr)) {                                    \
            fprintf(stderr, "%d: %s\n", __LINE__, #expr); \
            return 1;                                     \
        }                                                 \
    } while (0)

static ec_master_state_t observed = {.slaves_responding = 1, .al_states = 8, .link_up = 1};
static ec_slave_config_state_t configured = {.online = 1, .operational = 1, .al_state = 8};
static ec_domain_state_t domain = {.working_counter = 3, .wc_state = EC_WC_COMPLETE};
static uint8_t output[2240];
static uint8_t sent[2240];
static long now_ns = 10000000;

int __wrap_clock_gettime(clockid_t clock_id, struct timespec* now) {
    (void) clock_id;
    *now = (struct timespec){.tv_sec = 1, .tv_nsec = now_ns};
    return 0;
}
int __wrap_ecrt_master_receive(ec_master_t* master) {
    (void) master;
    return 0;
}
int __wrap_ecrt_domain_process(ec_domain_t* value) {
    (void) value;
    return 0;
}
int __wrap_ecrt_domain_queue(ec_domain_t* value) {
    (void) value;
    return 0;
}
int __wrap_ecrt_master_send(ec_master_t* master) {
    (void) master;
    memcpy(sent, output, sizeof(sent));
    return 0;
}
int __wrap_ecrt_domain_state(const ec_domain_t* value, ec_domain_state_t* state) {
    (void) value;
    *state = domain;
    return 0;
}
int __wrap_ecrt_master_state(const ec_master_t* master, ec_master_state_t* state) {
    (void) master;
    *state = observed;
    return 0;
}
int __wrap_ecrt_slave_config_state(const ec_slave_config_t* config,
                                   ec_slave_config_state_t* state) {
    (void) config;
    *state = configured;
    return 0;
}
static void set_offset(EcMotorOffsets* offsets, unsigned int base) {
    offsets->id = base;
    offsets->frame_flags = base + 4;
    offsets->dlc = base + 5;
    for (unsigned int i = 0; i < 8; ++i) {
        offsets->data[i] = base + 6 + i;
    }
}

static void write_input_slot(size_t base, size_t slot, uint32_t id, uint8_t flags, uint8_t dlc,
                             uint8_t data) {
    uint8_t* raw = output + base + slot * 14;
    for (unsigned int i = 0; i < 4; ++i) {
        raw[i] = (uint8_t) (id >> (8U * i));
    }
    raw[4] = flags;
    raw[5] = dlc;
    memset(raw + 6, data, 8);
}

typedef struct {
    EcCanFrame frames[16];
    uint16_t buses[16];
    size_t count;
} ReceivedFrames;

static void collect_frame(uint16_t slave, uint16_t bus, const EcCanFrame* frame, void* user) {
    ReceivedFrames* received = user;
    if (slave == 0 && received->count < 16) {
        received->frames[received->count] = *frame;
        received->buses[received->count++] = bus;
    }
}

static int test_can_fd_layout(size_t size) {
    EcMaster master = {0};
    master.initialized = true;
    master.domain_pd = output;
    master.slave_configs[0] = (ec_slave_config_t*) &master;
    master.layout.slave_count = 1;
    observed = (ec_master_state_t){.slaves_responding = 1, .al_states = 8, .link_up = 1};
    configured = (ec_slave_config_state_t){.online = 1, .operational = 1, .al_state = 8};
    domain = (ec_domain_state_t){.working_counter = 3, .wc_state = EC_WC_COMPLETE};
    CHECK(ec_identify_pdo_layout_sizes(size, size, &master.layout.slaves[0]));
    const size_t slots_per_bus = master.layout.slaves[0].slots_per_bus;
    for (size_t i = 0; i < size / 14; ++i) {
        set_offset(&master.offsets[0].out_motor[i], (unsigned int) (i * 14));
        set_offset(&master.offsets[0].in_motor[i], (unsigned int) (size + i * 14));
    }
    memset(output, 0, sizeof(output));
    CHECK(ec_master_cycle(&master));

    EcCanFrame frame = {.id = 0x1234567, .frame_flags = 0x03, .len = 64};
    for (size_t i = 0; i < sizeof(frame.data); ++i) {
        frame.data[i] = (uint8_t) i;
    }
    EcCanFrame short_frame = {.id = 0x354, .frame_flags = 0x08, .len = 3, .data = {1, 2, 3}};
    EcCanFrame invalid_frame = {.id = 0x123, .frame_flags = 0x0a, .len = 20};
    CHECK(!ec_master_send_can_frame(&master, 0, 0, &invalid_frame));
    invalid_frame.frame_flags = 0x04;
    invalid_frame.len = 3;
    CHECK(!ec_master_send_can_frame(&master, 0, 0, &invalid_frame));
    invalid_frame.frame_flags = 0x02;
    invalid_frame.len = 65;
    CHECK(!ec_master_send_can_frame(&master, 0, 0, &invalid_frame));
    CHECK(ec_master_send_can_frame(&master, 0, 0, &frame));
    CHECK(ec_master_send_can_frame(&master, 0, 0, &short_frame));
    CHECK(ec_master_send_can_frame(&master, 0, 1, &short_frame));
    MotorConfig motor = {0};
    MotorPackMsg packet = {.id = 0x455, .frame_flags = 0x06, .len = 3, .data = {4, 5, 6}};
    CHECK(ec_master_send_packet(&master, &motor, 3, &packet));
    packet.id = 0x466;
    CHECK(ec_master_send_packet(&master, &motor, 3, &packet));
    CHECK(ec_master_cycle(&master));
    for (size_t i = 0; i < slots_per_bus; ++i) {
        CHECK(sent[i * 14 + 5] == (i == 3 ? 3 : 0));
    }
    CHECK(sent[3 * 14] == 0x66 && sent[3 * 14 + 6] == 4);
    CHECK(sent[slots_per_bus * 14] == 0x54);
    CHECK(ec_master_cycle(&master));
    for (size_t i = 0; i < 8; ++i) {
        const uint8_t* slot = sent + i * 14;
        CHECK(slot[0] == 0x67 && slot[1] == 0x45 && slot[2] == 0x23 && slot[3] == 0x01);
        CHECK(slot[4] == (0x13 | i << 5) && slot[5] == 64);
        CHECK(memcmp(slot + 6, frame.data + i * 8, 8) == 0);
    }
    if (slots_per_bus == 8) {
        CHECK(ec_master_cycle(&master));
    }
    const uint8_t* tail = sent + (slots_per_bus == 8 ? 0 : 8 * 14);
    CHECK(tail[0] == 0x54 && tail[4] == 0x08 && tail[5] == 3 && tail[6] == 0);
    frame.len = 9;
    CHECK(ec_master_send_can_frame(&master, 0, 0, &frame));
    CHECK(ec_master_cycle(&master));
    CHECK(sent[14 + 4] == 0x33 && sent[14 + 5] == 9 && sent[14 + 6] == 8);
    for (size_t i = 7; i < 14; ++i) {
        CHECK(sent[14 + i] == 0);
    }

    ReceivedFrames received = {0};
    write_input_slot(size, 0, 0x123, 0x16, 20, 0xAA);
    write_input_slot(size, 1, 0x123, 0x56, 20, 0xCC);
    write_input_slot(size, 2, 0x124, 0x02, 3, 0xDD);
    CHECK(ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    CHECK(received.count == 1 && received.frames[0].id == 0x124);
    CHECK(received.frames[0].frame_flags == 0x02);
    memset(output + size, 0, size);
    write_input_slot(size, 0, 0x123, 0x56, 20, 0xCC);
    output[size + 10] = 0xEE;
    write_input_slot(size, 1, 0x123, 0x32, 20, 0xBB);
    write_input_slot(size, slots_per_bus, 0x123, 0x36, 20, 0xBB);
    write_input_slot(size, 2, 0x123, 0x36, 20, 0xBB);
    now_ns += 2000000;
    CHECK(ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    CHECK(received.count == 2 && received.buses[1] == 0);
    const EcCanFrame* assembled = &received.frames[1];
    CHECK(assembled->id == 0x123 && assembled->frame_flags == 0x06 && assembled->len == 20);
    CHECK(assembled->data[0] == 0xAA && assembled->data[8] == 0xBB && assembled->data[19] == 0xCC);
    CHECK(assembled->data[20] == 0);

    memset(output + size, 0, size);
    write_input_slot(size, 0, 0x123, 0x16, 20, 0xAA);
    write_input_slot(size, 1, 0x123, 0x56, 20, 0xCC);
    CHECK(ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    now_ns += 4000000;
    memset(output + size, 0, size);
    write_input_slot(size, 0, 0x123, 0x36, 20, 0xBB);
    CHECK(ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    CHECK(received.count == 2);

    memset(output + size, 0, size);
    write_input_slot(size, 0, 0x123, 0x16, 20, 0xAA);
    write_input_slot(size, 1, 0x123, 0x56, 20, 0xCC);
    CHECK(ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    domain.wc_state = EC_WC_INCOMPLETE;
    CHECK(!ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    domain.wc_state = EC_WC_COMPLETE;
    memset(output + size, 0, size);
    write_input_slot(size, 0, 0x123, 0x36, 20, 0xBB);
    CHECK(ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    CHECK(received.count == 2);

    CHECK(ec_master_send_can_frame(&master, 0, 0, &frame));
    configured.operational = 0;
    CHECK(!ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    configured.operational = 1;
    CHECK(ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    CHECK(sent[5] == 0);

    memset(output + size, 0, size);
    for (size_t i = 0; i < 4; ++i) {
        write_input_slot(size, i, 0x1FFFFFFF, (uint8_t) (0x13 | i << 5), 64, (uint8_t) i);
    }
    CHECK(ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    memset(output + size, 0, size);
    for (size_t i = 4; i < 8; ++i) {
        write_input_slot(size, i - 4, 0x1FFFFFFF, (uint8_t) (0x13 | i << 5), 64, (uint8_t) i);
    }
    CHECK(ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    CHECK(received.count == 3);
    CHECK(received.frames[2].id == 0x1FFFFFFF && received.frames[2].len == 64);
    CHECK(received.frames[2].frame_flags == 0x03);
    for (size_t i = 0; i < 64; ++i) {
        CHECK(received.frames[2].data[i] == i / 8);
    }

    memset(output + size, 0, size);
    write_input_slot(size, 0, 0x123, 0x16, 20, 0xAA);
    write_input_slot(size, 1, 0x123, 0x56, 20, 0xCC);
    write_input_slot(size, 2, 0x123, 0x56, 20, 0xEE);
    write_input_slot(size, 3, 0x123, 0x36, 20, 0xBB);
    CHECK(ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    CHECK(received.count == 3);

    memset(output + size, 0, size);
    write_input_slot(size, 0, 0x123, 0x16, 20, 0xAA);
    write_input_slot(size, 1, 0x123, 0x56, 20, 0xCC);
    CHECK(ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    memset(output + size, 0, size);
    CHECK(ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    write_input_slot(size, 0, 0x123, 0x36, 20, 0xBB);
    CHECK(ec_master_cycle_with_can_handler(&master, 2000, collect_frame, &received));
    CHECK(received.count == 3);
    return 0;
}

int main(void) {
    EcMaster master = {0};
    master.initialized = true;
    master.domain_pd = output;
    master.slave_configs[0] = (ec_slave_config_t*) &master;
    master.layout.slave_count = 1;
    CHECK(ec_identify_pdo_layout(86, &master.layout.slaves[0]));
    master.offsets[0].out_motor_num = 0;
    master.offsets[0].out_can_ide = 1;
    for (size_t i = 0; i < 6; ++i) {
        EcMotorOffsets* out = &master.offsets[0].out_motor[i];
        EcMotorOffsets* in = &master.offsets[0].in_motor[i];
        out->id = (unsigned int) (2 + i * 14);
        out->frame_flags = out->id + 4;
        out->dlc = out->id + 5;
        in->id = out->id + 86;
        in->frame_flags = out->frame_flags + 86;
        in->dlc = out->dlc + 86;
        for (size_t j = 0; j < 8; ++j) {
            out->data[j] = out->id + 6 + (unsigned int) j;
            in->data[j] = out->data[j] + 86;
        }
    }
    MotorConfig startup_config = {0};
    MotorPackMsg startup_packet = {0};
    CHECK(!ec_master_send_packet(&master, &startup_config, 0, &startup_packet));
    domain.working_counter = 0;
    CHECK(!ec_master_cycle(&master));
    CHECK(!master.operational);
    domain.working_counter = 3;
    CHECK(ec_master_cycle(&master));
    observed.link_up = 0;
    memset(output, 0x55, 86);
    CHECK(!ec_master_cycle(&master));
    for (size_t i = 0; i < 86; ++i) {
        CHECK(sent[i] == 0);
    }
    observed.link_up = 1;
    observed.al_states = 4;
    configured.operational = 0;
    configured.al_state = 4;
    CHECK(!ec_master_cycle(&master));
    observed.al_states = 12;
    CHECK(!ec_master_cycle(&master));
    observed.al_states = 8;
    configured.operational = 1;
    configured.al_state = 8;
    observed.slaves_responding = 0;
    CHECK(!ec_master_cycle(&master));
    observed.slaves_responding = 1;
    domain.wc_state = EC_WC_INCOMPLETE;
    CHECK(!ec_master_cycle(&master));
    domain.wc_state = EC_WC_COMPLETE;
    CHECK(ec_master_cycle(&master));
    CHECK(master.operational);
    master.wkc_error_count = 0;
    master.wkc_error_iteration = 0;
    domain.wc_state = EC_WC_INCOMPLETE;
    for (int i = 0; i < 19; ++i) {
        CHECK(!ec_master_cycle(&master));
        CHECK(master.operational);
    }
    CHECK(!ec_master_cycle(&master));
    CHECK(!master.operational);
    MotorConfig config = {0};
    MotorPackMsg packet = {0};
    CHECK(!ec_master_send_packet(&master, &config, 0, &packet));
    for (int i = 0; i < 25; ++i) {
        CHECK(!ec_master_cycle(&master));
    }
    CHECK(master.wkc_error_count == 20);
    domain.wc_state = EC_WC_COMPLETE;
    CHECK(ec_master_cycle(&master));
    CHECK(master.operational);
    CHECK(master.wkc_error_count == 0);
    for (int i = 0; i < 19; ++i) {
        domain.wc_state = EC_WC_INCOMPLETE;
        CHECK(!ec_master_cycle(&master));
        domain.wc_state = EC_WC_COMPLETE;
        CHECK(ec_master_cycle(&master));
    }
    for (int i = 38; i < 100; ++i) {
        CHECK(ec_master_cycle(&master));
    }
    domain.wc_state = EC_WC_INCOMPLETE;
    CHECK(!ec_master_cycle(&master));
    CHECK(master.operational);
    CHECK(master.wkc_error_count == 1);
    domain.wc_state = EC_WC_COMPLETE;
    master.layout.slave_count = 2;
    observed.slaves_responding = 2;
    observed.al_states = 10;
    CHECK(ec_master_cycle(&master));
    configured.online = 0;
    CHECK(!ec_master_cycle(&master));
    configured.online = 1;
    CHECK(ec_master_cycle(&master));

    CHECK(test_can_fd_layout(336) == 0);
    CHECK(test_can_fd_layout(420) == 0);
    CHECK(test_can_fd_layout(896) == 0);
    CHECK(test_can_fd_layout(1120) == 0);
    return 0;
}
