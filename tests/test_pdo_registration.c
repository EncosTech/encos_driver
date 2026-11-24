// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <stdio.h>

#include "example_igh/ethercat_layer.h"

#define CHECK(expr)                                       \
    do {                                                  \
        if (!(expr)) {                                    \
            fprintf(stderr, "%d: %s\n", __LINE__, #expr); \
            return 1;                                     \
        }                                                 \
    } while (0)

static uint8_t domain_pd[1024];
static unsigned int pos_calls, requests, releases, activations, intervals, dc_calls,
    reference_calls;
static uint16_t slave_count = 1;
static int fail_dc_pos = -1;
static bool fail_reference, fail_interval;
static uint8_t handles[3];
static unsigned int expected_period = EC_DEFAULT_CYCLE_PERIOD_US;
static int32_t expected_shift;
static unsigned int input_entries = 105;
static uint8_t last_input_entry_bits = 32;

static const unsigned int output_sync = 4;
static const unsigned int input_sync = 5;
static const int output_base = 17;
static const int input_base = 521;

ec_master_t* __wrap_ecrt_request_master(unsigned int index) {
    ++requests;
    return index == 0 ? (ec_master_t*) domain_pd : NULL;
}

void __wrap_ecrt_release_master(ec_master_t* master) {
    ++releases;
    (void) master;
}

int __wrap_ecrt_master(ec_master_t* master, ec_master_info_t* info) {
    (void) master;
    *info = (ec_master_info_t){.slave_count = slave_count};
    return 0;
}

int __wrap_ecrt_master_get_slave(ec_master_t* master, uint16_t slave, ec_slave_info_t* info) {
    (void) master;
    *info = (ec_slave_info_t){.sync_count = 6, .vendor_id = 1, .product_code = 2};
    if (slave_count > 1 && slave == 0) {
        info->sync_count = 0; /* Unsupported slave before two gateways. */
    }
    return slave < slave_count ? 0 : -1;
}

int __wrap_ecrt_master_get_sync_manager(ec_master_t* master, uint16_t slave, uint8_t sync,
                                        ec_sync_info_t* info) {
    (void) master;
    (void) slave;
    *info = (ec_sync_info_t){0};
    if (sync == output_sync || sync == input_sync) {
        info->dir = sync == output_sync ? EC_DIR_OUTPUT : EC_DIR_INPUT;
        info->n_pdos = 1;
    }
    return 0;
}

int __wrap_ecrt_master_get_pdo(ec_master_t* master, uint16_t slave, uint8_t sync, uint16_t pos,
                               ec_pdo_info_t* info) {
    (void) master;
    (void) slave;
    *info = (ec_pdo_info_t){.index = 0x1a20, .n_entries = sync == input_sync ? input_entries : 105};
    return pos == 0 ? 0 : -1;
}

int __wrap_ecrt_master_get_pdo_entry(ec_master_t* master, uint16_t slave, uint8_t sync,
                                     uint16_t pdo, uint16_t pos, ec_pdo_entry_info_t* entry) {
    (void) master;
    (void) slave;
    (void) pdo;
    *entry = (ec_pdo_entry_info_t){.index = 0x6540, .subindex = 1, .bit_length = 32};
    if (sync == input_sync && (unsigned int) pos + 1 == input_entries) {
        entry->bit_length = last_input_entry_bits;
    }
    return 0;
}

ec_domain_t* __wrap_ecrt_master_create_domain(ec_master_t* master) {
    return (ec_domain_t*) master;
}

ec_slave_config_t* __wrap_ecrt_master_slave_config(ec_master_t* master, uint16_t alias,
                                                   uint16_t pos, uint32_t vendor,
                                                   uint32_t product) {
    (void) master;
    return alias == 0 && pos < slave_count && vendor == 1 && product == 2
               ? (ec_slave_config_t*) &handles[pos]
               : NULL;
}

int __wrap_ecrt_slave_config_pdos(ec_slave_config_t* config, unsigned int count,
                                  const ec_sync_info_t* syncs) {
    (void) config;
    (void) count;
    (void) syncs;
    return -1;
}

int __wrap_ecrt_slave_config_reg_pdo_entry_pos(ec_slave_config_t* config, uint8_t sync,
                                               unsigned int pdo, unsigned int entry,
                                               ec_domain_t* domain, unsigned int* bit_position) {
    (void) config;
    (void) domain;
    if (pdo != 0 || entry != 0 || bit_position != NULL) {
        return -1;
    }
    ++pos_calls;
    if (sync == output_sync) {
        return output_base;
    }
    return sync == input_sync ? input_base : -1;
}

int __wrap_ecrt_domain_reg_pdo_entry_list(ec_domain_t* domain, const ec_pdo_entry_reg_t* regs) {
    (void) domain;
    return regs[0].index == 0 ? 0 : -1;
}

int __wrap_ecrt_slave_config_dc(ec_slave_config_t* config, uint16_t assign, uint32_t cycle,
                                int32_t shift, uint32_t cycle1, int32_t shift1) {
    ++dc_calls;
    if (dc_calls > 2 || config != (ec_slave_config_t*) &handles[dc_calls] || assign != 0x0300 ||
        cycle != expected_period * 1000U || shift != expected_shift || cycle1 != 0 || shift1 != 0 ||
        (int) dc_calls == fail_dc_pos) {
        return -1;
    }
    return 0;
}

int __wrap_ecrt_master_select_reference_clock(ec_master_t* master, ec_slave_config_t* sc) {
    (void) master;
    ++reference_calls;
    return !fail_reference && dc_calls == 2 && sc == (ec_slave_config_t*) &handles[1] ? 0 : -1;
}

int __wrap_ecrt_master_set_send_interval(ec_master_t* master, size_t interval) {
    (void) master;
    ++intervals;
    return !fail_interval && interval == expected_period ? 0 : -1;
}

int __wrap_ecrt_master_activate(ec_master_t* master) {
    ++activations;
    if (slave_count == 3 && (intervals != 1 || reference_calls != 1)) {
        return -1;
    }
    (void) master;
    return 0;
}

uint8_t* __wrap_ecrt_domain_data(const ec_domain_t* domain) {
    (void) domain;
    return domain_pd;
}

int main(void) {
    EcMaster master;
    CHECK(ec_master_open(&master, "0"));
    CHECK(pos_calls == 2);
    CHECK(master.slave_configs[0] != NULL);
    CHECK(master.layout.slaves[0].format == EC_SLAVE_FORMAT_CAN_FD_3_BUS_10_SLOTS);
    const EcSlaveOffsets* offsets = &master.offsets[0];
    CHECK(offsets->out_motor[0].id == (unsigned int) output_base);
    CHECK(offsets->in_motor[10].id == (unsigned int) input_base + 140);
    CHECK(offsets->out_motor[20].id == (unsigned int) output_base + 280);
    CHECK(offsets->in_motor[29].data[7] == (unsigned int) input_base + 419);
    ec_master_close(&master);

    pos_calls = 0;
    input_entries = 84;
    CHECK(ec_master_open(&master, "0"));
    CHECK(master.layout.slaves[0].format == EC_SLAVE_FORMAT_NONE);
    CHECK(master.slave_configs[0] == NULL && pos_calls == 0);
    ec_master_close(&master);

    input_entries = 105;
    last_input_entry_bits = 31;
    CHECK(ec_master_open(&master, "0"));
    CHECK(master.layout.slaves[0].format == EC_SLAVE_FORMAT_NONE);
    CHECK(master.slave_configs[0] == NULL && pos_calls == 0);
    ec_master_close(&master);
    CHECK(dc_calls == 0 && reference_calls == 0 && intervals == 0);
    EcMasterOptions options = {
        .sync_mode = EC_SYNC_DC, .cycle_period_us = 2000, .sync0_shift_ns = 20000};
    CHECK(
        !ec_master_open_with_configs(&master, "0", NULL, 0, &options)); /* No supported gateway. */
    CHECK(requests == releases && activations == 3);

    slave_count = 3;
    last_input_entry_bits = 32;
    expected_period = options.cycle_period_us;
    expected_shift = options.sync0_shift_ns;
    CHECK(ec_master_open_with_configs(&master, "0", NULL, 0, &options));
    CHECK(activations == 4 && dc_calls == 2 && master.slave_configs[0] == NULL);
    ec_master_close(&master);

    fail_dc_pos = 2;
    dc_calls = 0;
    CHECK(!ec_master_open_with_configs(&master, "0", NULL, 0, &options));
    CHECK(dc_calls == 2 && reference_calls == 1 && requests == releases && !master.initialized);
    fail_dc_pos = -1;
    fail_reference = true;
    dc_calls = 0;
    CHECK(!ec_master_open_with_configs(&master, "0", NULL, 0, &options));
    CHECK(reference_calls == 2 && intervals == 1 && requests == releases);
    fail_reference = false;
    fail_interval = true;
    dc_calls = 0;
    CHECK(!ec_master_open_with_configs(&master, "0", NULL, 0, &options));
    CHECK(intervals == 2 && activations == 4 && requests == releases);

    const unsigned int previous_requests = requests;
    options.sync0_shift_ns = INT32_MIN;
    CHECK(!ec_master_open_with_configs(&master, "0", NULL, 0, &options));
    options.sync0_shift_ns = 0;
    options.cycle_period_us = 0;
    CHECK(!ec_master_open_with_configs(&master, "0", NULL, 0, &options));
    options.cycle_period_us = UINT32_MAX / 1000U + 1;
    CHECK(!ec_master_open_with_configs(&master, "0", NULL, 0, &options));
    CHECK(requests == previous_requests);
    return 0;
}
