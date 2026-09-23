#include <stdio.h>
#include <string.h>

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
static uint8_t output[86];
static uint8_t sent[86];
int __wrap_ecx_send_processdata(ecx_contextt* ctx) {
    (void) ctx;
    memcpy(sent, output, sizeof(sent));
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
int main(void) {
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
