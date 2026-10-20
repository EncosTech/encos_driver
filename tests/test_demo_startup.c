// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#define main demo_main
#include "../examples/main.c"
#undef main

static bool ready;
static bool config_seen;
static unsigned int sent;

bool __wrap_ec_master_open_with_configs(EcMaster* master, const char* name,
                                        const EcAdapterConfig* configs, size_t count) {
    (void) name;
    memset(master, 0, sizeof(*master));
    master->layout.slave_count = 1;
    (void) ec_identify_pdo_layout(86, &master->layout.slaves[0]);
    config_seen = configs == NULL && count == 0;
    return true;
}
bool __wrap_ec_master_cycle(EcMaster* master) {
    master->operational = ready;
    return ready;
}
bool __wrap_ec_master_send_packet(EcMaster* master, const MotorConfig* config, uint16_t slot,
                                  const MotorPackMsg* packet) {
    (void) master;
    (void) config;
    (void) slot;
    (void) packet;
    ++sent;
    return ready;
}
void __wrap_ec_master_close(EcMaster* master) {
    (void) master;
}
void __wrap_demo_wait_until_next_cycle(struct timespec* next_cycle, long period_us) {
    (void) next_cycle;
    (void) period_us;
}
int main(void) {
    char* motor[] = {"demo", "0", "spd", "0", "0", "0", "11", "2", "1.0", "2.0", "1"};
    char* imu[] = {"demo", "0", "imu-monitor", "0", "0"};
    if (demo_main(11, motor) != 1 || !config_seen || sent != 0 || demo_main(5, imu) != 1 ||
        !config_seen) {
        fprintf(stderr, "Startup failure was not reported.\n");
        return 1;
    }
    ready = true;
    if (demo_main(11, motor) != 0 || sent != 1 || demo_main(5, imu) != 0 || !config_seen) {
        fprintf(stderr, "Successful startup failed.\n");
        return 1;
    }
    return 0;
}
