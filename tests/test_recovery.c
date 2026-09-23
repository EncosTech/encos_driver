#include <stdio.h>
#include <string.h>

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
static uint8_t output[256];
static uint8_t sent[256];
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

    return 0;
}
