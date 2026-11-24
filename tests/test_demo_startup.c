// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#define main demo_main
#include "../examples/main.c"
#undef main

static bool ready;
static bool config_seen;
static unsigned int sent;

static EcMasterOptions opened_options;
static int requested_priority;
static bool deny_fifo;
static unsigned int opens, closes, cycles, fifo_calls;
static bool wrong_period;

int __wrap_sched_setscheduler(pid_t pid, int policy, const struct sched_param* param) {
    ++fifo_calls;
    if (cycles != 0 || pid != 0 || policy != SCHED_FIFO || deny_fifo) {
        errno = EPERM;
        return -1;
    }
    requested_priority = param->sched_priority;
    return 0;
}

bool __wrap_ec_master_open_with_configs(EcMaster* master, const char* name,
                                        const EcAdapterConfig* configs, size_t count,
                                        const EcMasterOptions* options) {
    ++opens;
    cycles = 0;
    fifo_calls = 0;
    requested_priority = 0;
    opened_options = *options;
    (void) name;
    memset(master, 0, sizeof(*master));
    master->layout.slave_count = 1;
    (void) ec_identify_pdo_layout(86, &master->layout.slaves[0]);
    config_seen = configs == NULL && count == 0;
    return true;
}
bool __wrap_ec_master_cycle(EcMaster* master) {
    ++cycles;
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
    ++closes;
    (void) master;
}
void __wrap_demo_wait_until_next_cycle(struct timespec* next_cycle, long period_us) {
    (void) next_cycle;
    wrong_period |= period_us != opened_options.cycle_period_us;
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
    if (fifo_calls != 0 || opened_options.sync_mode != EC_SYNC_SM ||
        opened_options.cycle_period_us != 1000) {
        return 1;
    }

    char* dc_imu[] = {
        "demo",   "0",           "--sync", "dc", "--period-us", "2000", "--sync0-shift-ns",
        "-50000", "imu-monitor", "0",      "0"};
    char* dc_motor[] = {"demo", "0", "--sync", "dc", "--rt-priority", "70", "spd", "0",
                        "0",    "0", "11",     "2",  "1.0",           "2.0"};
    if (demo_main(11, dc_imu) != 0 || requested_priority != 80 || fifo_calls != 1 ||
        opened_options.sync_mode != EC_SYNC_DC || opened_options.cycle_period_us != 2000 ||
        opened_options.sync0_shift_ns != -50000 || wrong_period || demo_main(14, dc_motor) != 0 ||
        requested_priority != 70 || fifo_calls != 1 || sent != 2) {
        fprintf(stderr, "DC scheduling or options incorrect.\n");
        return 1;
    }
    deny_fifo = true;
    if (demo_main(14, dc_motor) != 1 || cycles != 0 || sent != 2 || opens != closes ||
        demo_main(11, dc_imu) != 1 || cycles != 0 || opens != closes) {
        fprintf(stderr, "FIFO failure did not abort the demo.\n");
        return 1;
    }
    dc_motor[5] = "0";
    if (demo_main(14, dc_motor) != 0 || fifo_calls != 0 || sent != 3) {
        return 1;
    }
    const unsigned int previous_opens = opens;
    dc_motor[5] = "100";
    if (demo_main(14, dc_motor) != 2) {
        return 1;
    }
    dc_imu[7] = "2000000";
    if (demo_main(11, dc_imu) != 2) {
        return 1;
    }
    dc_imu[7] = "0";
    dc_imu[3] = "sm";
    if (demo_main(11, dc_imu) != 2) {
        return 1;
    }
    dc_imu[3] = "dc";
    dc_imu[5] = "4294968";
    if (demo_main(11, dc_imu) != 2 || opens != previous_opens) {
        return 1;
    }
    return 0;
}
