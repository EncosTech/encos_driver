// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <math.h>
#include <stdio.h>

#include "example_soem/ethercat_layer.h"

#define CHECK(expr)                                       \
    do {                                                  \
        if (!(expr)) {                                    \
            fprintf(stderr, "%d: %s\n", __LINE__, #expr); \
            return 1;                                     \
        }                                                 \
    } while (0)

static unsigned int calls;
static unsigned int fail_at;
static bool bad;
static const uint16_t expected_slave = 1;
static const uint16_t expected_bus = 2;

static bool record(uint16_t index, uint8_t sub, int size, uint32_t value) {
    const uint32_t expected[] = {500000, 2000000, 751, 875, 1};
    ++calls;
    if (index != 0x2300 + expected_bus || sub != calls || sub > 5 || size != (sub == 5 ? 1 : 4) ||
        value != expected[sub - 1]) {
        bad = true;
    }
    return calls != fail_at;
}
int __wrap_ecx_SDOwrite(ecx_contextt* context, uint16_t slave, uint16_t index, uint8_t sub,
                        boolean complete, int size, void* data, int timeout) {
    (void) context;
    (void) timeout;
    const uint8_t* bytes = data;
    uint32_t value = bytes[0];
    if (size == 4) {
        value |= (uint32_t) bytes[1] << 8;
        value |= (uint32_t) bytes[2] << 16;
        value |= (uint32_t) bytes[3] << 24;
    }
    if (slave != expected_slave + 1 || complete) {
        bad = true;
    }
    return record(index, sub, size, value) ? 1 : 0;
}

int main(void) {
    EcMaster master = {0};
    master.layout.slave_count = 2;
    master.layout.slaves[1].bus_count = 8;

    EcAdapterConfig config = {.slave_id = expected_slave,
                              .bus_id = expected_bus,
                              .bitrate = 500000,
                              .dbitrate = 2000000,
                              .sample_point = 0.7506,
                              .dsample_point = 0.875};
    CHECK(ec_master_configure_adapters(&master, NULL, 0));
    CHECK(calls == 0);
    CHECK(ec_master_configure_adapters(&master, &config, 1));
    CHECK(calls == 5 && !bad);
    for (unsigned int failure = 1; failure <= 5; ++failure) {
        calls = 0;
        fail_at = failure;
        CHECK(!ec_master_configure_adapters(&master, &config, 1));
        CHECK(calls == failure && !bad);
    }
    calls = 0;
    fail_at = 0;
    EcAdapterConfig configs[] = {config, config};
    CHECK(!ec_master_configure_adapters(&master, configs, 2));
    configs[1].bus_id = 8;
    CHECK(!ec_master_configure_adapters(&master, configs, 2));
    CHECK(calls == 0);
    config.slave_id = 16;
    CHECK(!ec_master_configure_adapters(&master, &config, 1));
    config.slave_id = 1;
    config.bitrate = 0;
    CHECK(!ec_master_configure_adapters(&master, &config, 1));
    config.bitrate = 500000;
    config.dbitrate = 0;
    CHECK(!ec_master_configure_adapters(&master, &config, 1));
    config.dbitrate = 2000000;
    const double invalid[] = {0.0, 1.0, -0.1, NAN, INFINITY};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        config.sample_point = invalid[i];
        CHECK(!ec_master_configure_adapters(&master, &config, 1));
        config.sample_point = 0.7506;
        config.dsample_point = invalid[i];
        CHECK(!ec_master_configure_adapters(&master, &config, 1));
        config.dsample_point = 0.875;
    }
    CHECK(!ec_master_configure_adapters(NULL, &config, 1));
    CHECK(!ec_master_configure_adapters(&master, NULL, 1));
    master.initialized = true;
    CHECK(!ec_master_configure_adapters(&master, &config, 1));
    CHECK(calls == 0);
    return 0;
}
