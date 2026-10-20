// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <math.h>
#include <stdio.h>

#include "example_soem/ethercat_layer.h"

static bool valid_config(const EcMaster* master, const EcAdapterConfig* config) {
    return config->slave_id < master->layout.slave_count && config->slave_id < 16 &&
           config->bus_id < 8 &&
           config->bus_id < master->layout.slaves[config->slave_id].bus_count &&
           config->bitrate > 0 && config->dbitrate > 0 && isfinite(config->sample_point) &&
           config->sample_point > 0.0 && config->sample_point < 1.0 &&
           isfinite(config->dsample_point) && config->dsample_point > 0.0 &&
           config->dsample_point < 1.0;
}

bool ec_master_configure_adapters(EcMaster* master, const EcAdapterConfig* configs, size_t count) {
    if (master == NULL || master->initialized || (count != 0 && configs == NULL)) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!valid_config(master, &configs[i])) {
            fprintf(stderr, "Invalid CAN timing config at entry %zu.\n", i);
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (configs[i].slave_id == configs[j].slave_id &&
                configs[i].bus_id == configs[j].bus_id) {
                fprintf(stderr, "Duplicate CAN timing target at entry %zu.\n", i);
                return false;
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const EcAdapterConfig* config = &configs[i];
        const uint16_t index = (uint16_t) (0x2300U + config->bus_id);
        const uint32_t values[] = {
            config->bitrate,
            config->dbitrate,
            (uint32_t) (config->sample_point * 1000.0 + 0.5),
            (uint32_t) (config->dsample_point * 1000.0 + 0.5),
            1,
        };
        for (uint8_t sub = 1; sub <= 5; ++sub) {
            const uint32_t value = values[sub - 1];
            uint8_t data[4] = {(uint8_t) value, (uint8_t) (value >> 8), (uint8_t) (value >> 16),
                               (uint8_t) (value >> 24)};
            const bool ok = ecx_SDOwrite(&master->ctx, (uint16_t) (config->slave_id + 1), index,
                                         sub, FALSE, sub == 5 ? 1 : 4, data, EC_TIMEOUTRXM) > 0;
            if (!ok) {
                fprintf(stderr, "CAN timing SDO failed: slave=%u bus=%u index=0x%04x sub=%u.\n",
                        config->slave_id, config->bus_id, index, sub);
                fprintf(stderr,
                        "Check the EC-to-CAN model and firmware version. If the version "
                        "is too old or CAN timing SDO is unsupported, set "
                        "enableAdapterConfig = false in examples/main.c and rebuild "
                        "before using the adapter.\n");
                return false;
            }
        }
    }
    return true;
}
