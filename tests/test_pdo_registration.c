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
static unsigned int pos_calls;
static unsigned int input_entries = 105;
static uint8_t last_input_entry_bits = 32;

static const unsigned int output_sync = 4;
static const unsigned int input_sync = 5;
static const int output_base = 17;
static const int input_base = 521;

ec_master_t* __wrap_ecrt_request_master(unsigned int index) {
    return index == 0 ? (ec_master_t*) domain_pd : NULL;
}

void __wrap_ecrt_release_master(ec_master_t* master) {
    (void) master;
}

int __wrap_ecrt_master(ec_master_t* master, ec_master_info_t* info) {
    (void) master;
    *info = (ec_master_info_t){.slave_count = 1};
    return 0;
}

int __wrap_ecrt_master_get_slave(ec_master_t* master, uint16_t slave, ec_slave_info_t* info) {
    (void) master;
    *info = (ec_slave_info_t){.sync_count = 6, .vendor_id = 1, .product_code = 2};
    return slave == 0 ? 0 : -1;
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
    return alias == 0 && pos == 0 && vendor == 1 && product == 2 ? (ec_slave_config_t*) master
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

int __wrap_ecrt_master_activate(ec_master_t* master) {
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
    return 0;
}
