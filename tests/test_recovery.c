// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "example_soem/ethercat_layer.h"

#define CHECK(expr)                                       \
    do {                                                  \
        if (!(expr)) {                                    \
            fprintf(stderr, "%d: %s\n", __LINE__, #expr); \
            return 1;                                     \
        }                                                 \
    } while (0)

static int observed_state = EC_STATE_OPERATIONAL;
static int received_wkc = 3;
static int writes;
static int reconfigs;
static int recovers;
static uint64_t now_ns;
static uint8_t output[86];
static uint8_t sent[1120];

int __wrap_clock_gettime(clockid_t clock, struct timespec* value) {
    (void) clock;
    value->tv_sec = (time_t) (now_ns / 1000000000ULL);
    value->tv_nsec = (long) (now_ns % 1000000000ULL);
    return 0;
}

int __wrap_ecx_send_processdata(ecx_contextt* ctx) {
    memcpy(sent, ctx->slavelist[1].outputs, ctx->slavelist[1].Obytes);
    return 1;
}
int __wrap_ecx_receive_processdata(ecx_contextt* ctx, int timeout) {
    (void) ctx;
    (void) timeout;
    return received_wkc;
}
int __wrap_ecx_readstate(ecx_contextt* ctx) {
    ctx->slavelist[1].state = (uint16_t) observed_state;
    return observed_state;
}
int __wrap_ecx_writestate(ecx_contextt* ctx, uint16_t slave) {
    (void) ctx;
    (void) slave;
    ++writes;
    return 1;
}
uint16_t __wrap_ecx_statecheck(ecx_contextt* ctx, uint16_t slave, uint16_t state, int timeout) {
    (void) ctx;
    (void) slave;
    (void) state;
    (void) timeout;
    return (uint16_t) observed_state;
}
int __wrap_ecx_reconfig_slave(ecx_contextt* ctx, uint16_t slave, int timeout) {
    (void) ctx;
    (void) slave;
    (void) timeout;
    ++reconfigs;
    return EC_STATE_SAFE_OP;
}
int __wrap_ecx_recover_slave(ecx_contextt* ctx, uint16_t slave, int timeout) {
    (void) ctx;
    (void) slave;
    (void) timeout;
    ++recovers;
    return 1;
}
static bool cycle(EcMaster* master) {
    master->next_state_check_ns = 0;
    return ec_master_cycle(master);
}
static int test_link_recovery(void) {
    EcMaster master = {0};
    master.initialized = true;
    master.ctx.slavecount = 1;
    master.ctx.slavelist[1].outputs = output;
    master.ctx.slavelist[1].Obytes = sizeof(output);
    master.expected_wkc = 3;
    master.layout.slave_count = 1;
    CHECK(ec_identify_pdo_layout(sizeof(output), &master.layout.slaves[0]));
    MotorConfig startup_config = {0};
    MotorPackMsg startup_packet = {0};
    CHECK(!ec_master_send_packet(&master, &startup_config, 0, &startup_packet));
    master.expected_wkc = 0;
    CHECK(!cycle(&master));
    CHECK(!master.operational);
    master.expected_wkc = 3;
    CHECK(cycle(&master));
    MotorPackMsg command = {.id = 1, .len = 1, .data = {0x55}};
    CHECK(ec_master_send_packet(&master, &startup_config, 0, &command));
    CHECK(cycle(&master));
    CHECK(sent[0] == 1 && sent[2] == 1);
    CHECK(output[0] == 0 && output[2] == 0);
    CHECK(cycle(&master));
    CHECK(sent[0] == 0 && sent[2] == 0);
    observed_state = EC_STATE_SAFE_OP + EC_STATE_ERROR;
    received_wkc = 0;
    memset(output, 0x55, sizeof(output));
    CHECK(!cycle(&master));
    CHECK(writes > 0);
    CHECK(sent[0] == 0 && sent[85] == 0);
    observed_state = EC_STATE_SAFE_OP;
    received_wkc = 3;
    CHECK(!cycle(&master));
    CHECK(writes > 1);
    master.next_state_check_ns = UINT64_MAX;
    CHECK(!ec_master_cycle(&master));
    CHECK(!master.operational);
    observed_state = EC_STATE_OPERATIONAL;
    CHECK(!ec_master_cycle(&master));
    CHECK(!master.operational);
    CHECK(cycle(&master));
    CHECK(master.operational);
    observed_state = EC_STATE_PRE_OP;
    CHECK(!cycle(&master));
    CHECK(reconfigs > 0);
    observed_state = EC_STATE_NONE;
    CHECK(!cycle(&master));
    CHECK(recovers > 0);
    observed_state = EC_STATE_OPERATIONAL;
    received_wkc = 0;
    CHECK(!cycle(&master));
    received_wkc = 3;
    CHECK(cycle(&master));
    CHECK(master.operational);
    master.wkc_error_count = 0;
    master.wkc_error_iteration = 0;
    received_wkc = 0;
    for (int i = 0; i < 19; ++i) {
        CHECK(!cycle(&master));
        CHECK(master.operational);
    }
    CHECK(!cycle(&master));
    CHECK(!master.operational);
    MotorConfig config = {0};
    MotorPackMsg packet = {0};
    CHECK(!ec_master_send_packet(&master, &config, 0, &packet));
    for (int i = 0; i < 25; ++i) {
        CHECK(!cycle(&master));
    }
    CHECK(master.wkc_error_count == 20);
    received_wkc = 3;
    CHECK(cycle(&master));
    CHECK(master.operational);
    CHECK(master.wkc_error_count == 0);
    for (int i = 0; i < 19; ++i) {
        received_wkc = 0;
        CHECK(!cycle(&master));
        received_wkc = 3;
        CHECK(cycle(&master));
    }
    for (int i = 38; i < 100; ++i) {
        CHECK(cycle(&master));
    }
    received_wkc = 0;
    CHECK(!cycle(&master));
    CHECK(master.operational);
    CHECK(master.wkc_error_count == 1);
    return 0;
}

typedef struct {
    size_t count;
    uint16_t buses[16];
    EcCanFrame frames[16];
} CapturedFrames;

static void capture_frame(uint16_t slave, uint16_t bus, const EcCanFrame* frame, void* user_data) {
    CapturedFrames* capture = user_data;
    if (slave == 0 && capture->count < 16) {
        capture->buses[capture->count] = bus;
        capture->frames[capture->count++] = *frame;
    }
}

static void prepare_can_master(EcMaster* master, uint8_t* inputs, uint8_t* outputs, size_t size) {
    master->initialized = true;
    master->ctx.slavecount = 1;
    master->ctx.slavelist[1].inputs = inputs;
    master->ctx.slavelist[1].Ibytes = (uint32_t) size;
    master->ctx.slavelist[1].outputs = outputs;
    master->ctx.slavelist[1].Obytes = (uint32_t) size;
    master->expected_wkc = 3;
    master->layout.slave_count = 1;
    ec_identify_pdo_layout_sizes(size, size, &master->layout.slaves[0]);
    received_wkc = 3;
    observed_state = EC_STATE_OPERATIONAL;
    now_ns = 0;
}

static bool capture_cycle(EcMaster* master, CapturedFrames* capture, uint32_t elapsed_us) {
    now_ns += 1000ULL * elapsed_us;
    master->next_state_check_ns = 0;
    return ec_master_cycle_with_can_handler(master, 1000, capture_frame, capture);
}

static EcCanFrame sample_frame(uint32_t id, uint8_t flags, uint8_t len) {
    EcCanFrame frame = {.id = id, .frame_flags = flags, .len = len};
    for (uint8_t i = 0; i < len; ++i) {
        frame.data[i] = (uint8_t) (0x31 + i);
    }
    return frame;
}

static void write_fragment(uint8_t* slot, const EcCanFrame* frame, uint8_t index) {
    memset(slot, 0, 14);
    for (unsigned int i = 0; i < 4; ++i) {
        slot[i] = (uint8_t) (frame->id >> (8 * i));
    }
    slot[4] = frame->frame_flags | (uint8_t) (0x10 | (index << 5));
    slot[5] = frame->len;
    for (size_t i = 0; i < 8 && 8U * index + i < frame->len; ++i) {
        slot[6 + i] = frame->data[8U * index + i];
    }
}

static int test_can_fd_layout_roundtrip(size_t size) {
    EcMaster master = {0};
    uint8_t inputs[1120] = {0};
    uint8_t outputs[1120] = {0};
    CapturedFrames capture = {0};
    prepare_can_master(&master, inputs, outputs, size);
    CHECK(capture_cycle(&master, &capture, 1000));
    const uint16_t slots = (uint16_t) master.layout.slaves[0].slots_per_bus;
    const size_t bus_offset = slots * 14U;
    const EcCanFrame long_frame =
        sample_frame(0x1ABCDEF, MOTOR_CAN_FLAG_EFF | MOTOR_CAN_FLAG_FD_BIT1, 64);
    const EcCanFrame short_frame = sample_frame(0x234, MOTOR_CAN_FLAG_FD_MASK, 1);
    for (uint16_t i = 0; i < slots - 7; ++i) {
        CHECK(ec_master_send_can_frame(&master, 0, 1, &short_frame));
    }
    CHECK(ec_master_send_can_frame(&master, 0, 1, &long_frame));
    CHECK(ec_master_send_can_frame(&master, 0, 1, &short_frame));
    CHECK(ec_master_send_can_frame(&master, 0, 2, &short_frame));
    CHECK(capture_cycle(&master, &capture, 1000));
    for (uint16_t i = 0; i < slots; ++i) {
        CHECK(sent[bus_offset + 14U * i + 5U] == (i < slots - 7 ? 1 : 0));
    }
    CHECK(sent[2 * bus_offset + 5] == 1);
    CHECK(capture_cycle(&master, &capture, 1000));
    for (unsigned int i = 0; i < 8; ++i) {
        const uint8_t* slot = sent + bus_offset + 14U * i;
        CHECK(slot[0] == 0xEF && slot[1] == 0xCD && slot[2] == 0xAB && slot[3] == 0x01);
        CHECK(slot[4] == (0x13U | (i << 5)) && slot[5] == 64);
        CHECK(memcmp(slot + 6, long_frame.data + 8U * i, 8) == 0);
    }
    if (slots == 10) {
        CHECK(sent[bus_offset + 8 * 14 + 5] == 1);
    }
    memcpy(inputs, sent, size);
    CHECK(capture_cycle(&master, &capture, 1000));
    CHECK(capture.count == (slots == 10 ? 2U : 1U));
    CHECK(capture.buses[0] == 1);
    CHECK(capture.frames[0].id == long_frame.id);
    CHECK(capture.frames[0].frame_flags == long_frame.frame_flags);
    CHECK(capture.frames[0].len == long_frame.len);
    CHECK(memcmp(capture.frames[0].data, long_frame.data, sizeof(long_frame.data)) == 0);
    if (slots == 8) {
        CHECK(sent[bus_offset + 5] == 1);
    }
    return 0;
}

static int test_can_fd_fixed_slot_and_validation(void) {
    EcMaster master = {0};
    uint8_t inputs[420] = {0};
    uint8_t outputs[420] = {0};
    prepare_can_master(&master, inputs, outputs, sizeof(outputs));
    CHECK(cycle(&master));
    const MotorConfig motor = {0};
    const MotorPackMsg command = {.id = 0x123, .len = 1, .data = {0xAA}};
    const EcCanFrame long_frame = sample_frame(0x456, MOTOR_CAN_FLAG_FD_MASK, 20);
    const EcCanFrame short_frame = sample_frame(0x234, MOTOR_CAN_FLAG_FD_BIT1, 1);
    CHECK(ec_master_send_packet(&master, &motor, 0, &command));
    CHECK(ec_master_send_packet(&master, &motor, 2, &command));
    CHECK(ec_master_send_packet(&master, &motor, 9, &command));
    CHECK(ec_master_send_can_frame(&master, 0, 0, &long_frame));
    CHECK(ec_master_send_can_frame(&master, 0, 0, &short_frame));
    CHECK(cycle(&master));
    CHECK(sent[1 * 14 + 5] == 0);
    CHECK(sent[3 * 14 + 4] == 0x16 && sent[3 * 14 + 5] == 20);
    CHECK(sent[4 * 14 + 4] == 0x36 && sent[4 * 14 + 5] == 20);
    CHECK(sent[5 * 14 + 4] == 0x56 && sent[5 * 14 + 5] == 20);
    CHECK(memcmp(sent + 5 * 14 + 6, long_frame.data + 16, 4) == 0);
    CHECK(sent[5 * 14 + 10] == 0 && sent[5 * 14 + 13] == 0);
    CHECK(sent[6 * 14 + 5] == 1);
    CHECK(sent[9 * 14 + 5] == 1 && sent[9 * 14 + 6] == 0xAA);

    const EcCanFrame invalid[] = {
        {.id = 0, .len = 1},
        {.id = 0x800, .len = 1},
        {.id = 0x20000000, .frame_flags = MOTOR_CAN_FLAG_EFF, .len = 1},
        {.id = 1, .len = 0},
        {.id = 1, .frame_flags = MOTOR_CAN_FLAG_FD_MASK, .len = 65},
        {.id = 1, .frame_flags = MOTOR_CAN_FLAG_FD_BIT2, .len = 1},
        {.id = 1, .frame_flags = MOTOR_CAN_FLAG_FD_MASK | MOTOR_CAN_FLAG_RTR, .len = 1},
        {.id = 1, .len = 9},
        {.id = 1, .frame_flags = 0x12, .len = 20},
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(!ec_master_send_can_frame(&master, 0, 0, &invalid[i]));
    }
    const EcCanFrame rtr = sample_frame(0x234, MOTOR_CAN_FLAG_RTR, 7);
    CHECK(ec_master_send_can_frame(&master, 0, 0, &rtr));
    CHECK(cycle(&master));
    CHECK(sent[4] == MOTOR_CAN_FLAG_RTR && sent[5] == 7);
    for (size_t i = 6; i < 14; ++i) {
        CHECK(sent[i] == 0);
    }
    return 0;
}

static int test_can_fd_reassembly(void) {
    EcMaster master = {0};
    uint8_t inputs[420] = {0};
    uint8_t outputs[420] = {0};
    CapturedFrames capture = {0};
    prepare_can_master(&master, inputs, outputs, sizeof(outputs));
    const EcCanFrame a = sample_frame(0x123, MOTOR_CAN_FLAG_FD_MASK, 20);
    const EcCanFrame b = sample_frame(0x123, MOTOR_CAN_FLAG_FD_BIT1, 20);
    write_fragment(inputs, &a, 0);
    write_fragment(inputs + 14, &b, 0);
    write_fragment(inputs + 28, &a, 2);
    write_fragment(inputs + 42, &a, 2);
    inputs[42 + 13] = 0x80;
    CHECK(capture_cycle(&master, &capture, 1000));
    CHECK(capture.count == 0);
    memset(inputs, 0, sizeof(inputs));
    write_fragment(inputs, &b, 1);
    write_fragment(inputs + 14, &a, 1);
    write_fragment(inputs + 28, &b, 2);
    write_fragment(inputs + 140, &a, 1);
    CHECK(capture_cycle(&master, &capture, 1000));
    CHECK(capture.count == 2 && capture.buses[0] == 0 && capture.buses[1] == 0);
    CHECK(capture.frames[0].frame_flags == a.frame_flags);
    CHECK(capture.frames[1].frame_flags == b.frame_flags);
    CHECK(capture.frames[0].len == a.len && capture.frames[1].len == b.len);
    CHECK(memcmp(capture.frames[0].data, a.data, sizeof(a.data)) == 0);
    CHECK(memcmp(capture.frames[1].data, b.data, sizeof(b.data)) == 0);

    memset(inputs, 0, sizeof(inputs));
    write_fragment(inputs, &a, 0);
    write_fragment(inputs + 14, &a, 1);
    write_fragment(inputs + 28, &a, 1);
    inputs[28 + 6] ^= 1;
    write_fragment(inputs + 42, &a, 2);
    write_fragment(inputs + 56, &b, 0);
    write_fragment(inputs + 70, &b, 3);
    write_fragment(inputs + 84, &b, 1);
    write_fragment(inputs + 98, &b, 2);
    CHECK(capture_cycle(&master, &capture, 1000));
    CHECK(capture.count == 2);

    memset(inputs, 0, sizeof(inputs));
    write_fragment(inputs, &a, 0);
    write_fragment(inputs + 14, &a, 1);
    CHECK(capture_cycle(&master, &capture, 1000));
    memset(inputs, 0, sizeof(inputs));
    write_fragment(inputs, &a, 2);
    CHECK(capture_cycle(&master, &capture, 2000));
    CHECK(capture.count == 2);

    memset(inputs, 0, sizeof(inputs));
    write_fragment(inputs, &a, 0);
    write_fragment(inputs + 14, &a, 1);
    CHECK(capture_cycle(&master, &capture, 100));
    memset(inputs, 0, sizeof(inputs));
    CHECK(capture_cycle(&master, &capture, 100));
    write_fragment(inputs, &a, 2);
    CHECK(capture_cycle(&master, &capture, 100));
    CHECK(capture.count == 2);

    memset(inputs, 0, sizeof(inputs));
    write_fragment(inputs, &a, 0);
    write_fragment(inputs + 14, &a, 1);
    CHECK(capture_cycle(&master, &capture, 100));
    received_wkc = 0;
    CHECK(!capture_cycle(&master, &capture, 100));
    received_wkc = 3;
    memset(inputs, 0, sizeof(inputs));
    write_fragment(inputs, &a, 2);
    CHECK(capture_cycle(&master, &capture, 100));
    CHECK(capture.count == 2);

    memset(inputs, 0, sizeof(inputs));
    write_fragment(inputs, &a, 0);
    write_fragment(inputs + 14, &a, 1);
    CHECK(capture_cycle(&master, &capture, 100));
    const EcCanFrame queued = sample_frame(0x124, MOTOR_CAN_FLAG_FD_MASK, 64);
    CHECK(ec_master_send_can_frame(&master, 0, 0, &queued));
    observed_state = EC_STATE_SAFE_OP;
    CHECK(!capture_cycle(&master, &capture, 100));
    observed_state = EC_STATE_OPERATIONAL;
    memset(inputs, 0, sizeof(inputs));
    write_fragment(inputs, &a, 2);
    CHECK(capture_cycle(&master, &capture, 100));
    CHECK(capture.count == 2);
    CHECK(capture_cycle(&master, &capture, 100));
    CHECK(sent[5] == 0);
    return 0;
}

int main(void) {
    CHECK(test_link_recovery() == 0);
    CHECK(test_can_fd_layout_roundtrip(336) == 0);
    CHECK(test_can_fd_layout_roundtrip(420) == 0);
    CHECK(test_can_fd_layout_roundtrip(896) == 0);
    CHECK(test_can_fd_layout_roundtrip(1120) == 0);
    CHECK(test_can_fd_fixed_slot_and_validation() == 0);
    CHECK(test_can_fd_reassembly() == 0);
    return 0;
}
