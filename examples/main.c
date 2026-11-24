// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include "example_igh/demo_timing.h"
#include "example_igh/ethercat_layer.h"

static const long kDemoCyclePeriodUs = 1000;
static const int kDcFifoPriority = 80;
static const int kDemoCycleCount = 5000;

/**
 * @brief 打印程序用法信息
 * @param[in] program 程序名
 */
static void print_usage(const char* program) {
    fprintf(stderr,
            "Usage:\n"
            "  %s <masterIndex> [global options] <op> <slaveId> <busId> <slot> <motorId> <model> "
            "[--eff] [--canfd] [--flag <value>] [args...]\n"
            "\n"
            "masterIndex selects /dev/EtherCAT<masterIndex>, for example 0.\n"
            "Global option: --sync sm|dc. DC options: --period-us N --sync0-shift-ns N "
            "--rt-priority 0..99\n"
            "Defaults: SM, 1000 us, shift 0 ns; DC requests FIFO 80 (0 skips scheduling).\n"
            "Frame defaults are classic CAN standard frames: eff=0 canfd=0.\n"
            "Indexes are all zero based. Supported ops:\n"
            "  spd <speed_rad_s> <current_a> [feedback]\n"
            "  pos <position_rad> <speed_rad_s> <current_a> [feedback]\n"
            "  cur <current_a> [feedback]\n"
            "  tor <torque_nm> [feedback]\n"
            "  pvt <kp> <kd> <position_rad> <speed_rad_s> <torque_nm>\n"
            "  stop <mode:2|3|4> <current_a> [feedback]\n"
            "  brake <0|1>\n"
            "  reset-zero\n"
            "  get-param <parameter_id>\n"
            "  imu-monitor <slaveId> <busId> [imuIndex:0-9]\n"
            "\n"
            "Example:\n"
            "  sudo %s 0 spd 0 2 0 11 2 1.0 2.0 1\n",
            program, program);
}

/**
 * @brief 将字符串解析为 uint16_t
 * @param[in] text 输入字符串
 * @return 解析后的 uint16_t 值
 */
static uint16_t parse_u16(const char* text) {
    return (uint16_t) strtoul(text, NULL, 0);
}

/**
 * @brief 将字符串解析为 float
 * @param[in] text 输入字符串
 * @return 解析后的 float 值
 */
static float parse_float(const char* text) {
    return strtof(text, NULL);
}

static bool parse_integer(const char* text, int64_t min, int64_t max, int64_t* value) {
    char* end = NULL;
    errno = 0;
    const intmax_t result = strtoimax(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || result < min || result > max) {
        return false;
    }
    *value = result;
    return true;
}

static bool parse_master_options(int argc, char** argv, int* op_index, EcMasterOptions* options,
                                 int* priority) {
    int index = 2;
    bool dc_options_set = false;
    while (index < argc && strncmp(argv[index], "--", 2) == 0) {
        if (index + 1 >= argc) {
            return false;
        }
        const char* name = argv[index++];
        const char* value = argv[index++];
        int64_t number;
        if (strcmp(name, "--sync") == 0) {
            if (strcmp(value, "dc") == 0) {
                options->sync_mode = EC_SYNC_DC;
            } else if (strcmp(value, "sm") == 0) {
                options->sync_mode = EC_SYNC_SM;
            } else {
                return false;
            }
        } else if (strcmp(name, "--period-us") == 0 &&
                   parse_integer(value, 1, UINT32_MAX / 1000U, &number) &&
                   number * 1000 + 999999999L <= LONG_MAX) {
            options->cycle_period_us = (uint32_t) number;
            dc_options_set = true;
        } else if (strcmp(name, "--sync0-shift-ns") == 0 &&
                   parse_integer(value, INT32_MIN, INT32_MAX, &number)) {
            options->sync0_shift_ns = (int32_t) number;
            dc_options_set = true;
        } else if (strcmp(name, "--rt-priority") == 0 && parse_integer(value, 0, 99, &number)) {
            *priority = (int) number;
            dc_options_set = true;
        } else {
            return false;
        }
    }
    const int64_t period_ns = (int64_t) options->cycle_period_us * 1000;
    if (*priority < 0) {
        *priority = options->sync_mode == EC_SYNC_DC ? kDcFifoPriority : 0;
    }
    *op_index = index;
    return index < argc && (!dc_options_set || options->sync_mode == EC_SYNC_DC) &&
           options->sync0_shift_ns > -period_ns && options->sync0_shift_ns < period_ns;
}

static bool set_dc_priority(int priority) {
    const struct sched_param param = {.sched_priority = priority};
    if (sched_setscheduler(0, SCHED_FIFO, &param) == 0) {
        return true;
    }
    const int error = errno;
    fprintf(stderr, "Cannot set cycle thread to SCHED_FIFO %d: %s. ", priority, strerror(error));
    struct rlimit limit;
    if (getrlimit(RLIMIT_RTPRIO, &limit) == 0) {
        fprintf(stderr, "RLIMIT_RTPRIO soft=%" PRIuMAX " hard=%" PRIuMAX ". ",
                (uintmax_t) limit.rlim_cur, (uintmax_t) limit.rlim_max);
    }
    fprintf(stderr, "Check rtprio or CAP_SYS_NICE; use --rt-priority 0 for debugging.\n");
    return false;
}

/**
 * @brief 解析命令行中的帧选项（--eff, --canfd, --flag）
 * @param[in] argc 参数个数
 * @param[in] argv 参数数组
 * @param[in,out] arg_index 当前参数索引，会被更新
 * @param[out] flags 解析后的帧标志
 * @return 解析成功返回 true，失败返回 false
 */
static bool parse_frame_options(int argc, char** argv, int* arg_index, uint8_t* flags) {
    while (*arg_index < argc) {
        if (strcmp(argv[*arg_index], "--eff") == 0) {
            *flags = (uint8_t) (*flags | MOTOR_CAN_FLAG_EFF);
            ++(*arg_index);
            continue;
        }
        if (strcmp(argv[*arg_index], "--canfd") == 0) {
            *flags = (uint8_t) (*flags | MOTOR_CAN_FLAG_FD_MASK);
            ++(*arg_index);
            continue;
        }
        if (strcmp(argv[*arg_index], "--flag") == 0) {
            if (*arg_index + 1 >= argc) {
                return false;
            }
            *flags = (uint8_t) parse_u16(argv[*arg_index + 1]);
            *arg_index += 2;
            continue;
        }
        break;
    }

    *flags = motor_sanitize_flags(*flags);
    return true;
}

/**
 * @brief 根据命令行参数构建操作报文
 * @param[in] argc 参数个数
 * @param[in] argv 参数数组
 * @param[in] arg_index 当前参数索引
 * @param[in] op 操作类型字符串
 * @param[in] config 电机配置
 * @param[out] packet 输出报文
 * @return 构建成功返回 true，失败返回 false
 */
static bool build_operation(int argc, char** argv, int arg_index, const char* op,
                            const MotorConfig* config, MotorPackMsg* packet) {
    if (strcmp(op, "spd") == 0 && argc >= arg_index + 2) {
        const uint8_t feedback =
            argc >= arg_index + 3 ? (uint8_t) parse_u16(argv[arg_index + 2]) : 0;
        return motor_build_spd_control(config, parse_float(argv[arg_index]),
                                       parse_float(argv[arg_index + 1]), feedback, packet);
    }
    if (strcmp(op, "pos") == 0 && argc >= arg_index + 3) {
        const uint8_t feedback =
            argc >= arg_index + 4 ? (uint8_t) parse_u16(argv[arg_index + 3]) : 0;
        return motor_build_pos_control(config, parse_float(argv[arg_index]),
                                       parse_float(argv[arg_index + 1]),
                                       parse_float(argv[arg_index + 2]), feedback, packet);
    }
    if (strcmp(op, "cur") == 0 && argc >= arg_index + 1) {
        const uint8_t feedback =
            argc >= arg_index + 2 ? (uint8_t) parse_u16(argv[arg_index + 1]) : 0;
        return motor_build_cur_control(config, parse_float(argv[arg_index]), feedback, packet);
    }
    if (strcmp(op, "tor") == 0 && argc >= arg_index + 1) {
        const uint8_t feedback =
            argc >= arg_index + 2 ? (uint8_t) parse_u16(argv[arg_index + 1]) : 0;
        return motor_build_tor_control(config, parse_float(argv[arg_index]), feedback, packet);
    }
    if (strcmp(op, "pvt") == 0 && argc >= arg_index + 5) {
        return motor_build_pvt_control(
            config, parse_float(argv[arg_index]), parse_float(argv[arg_index + 1]),
            parse_float(argv[arg_index + 2]), parse_float(argv[arg_index + 3]),
            parse_float(argv[arg_index + 4]), packet);
    }
    if (strcmp(op, "stop") == 0 && argc >= arg_index + 2) {
        const uint8_t feedback =
            argc >= arg_index + 3 ? (uint8_t) parse_u16(argv[arg_index + 2]) : 0;
        return motor_build_stop(config, (MotorStopMode) parse_u16(argv[arg_index]),
                                parse_float(argv[arg_index + 1]), feedback, packet);
    }
    if (strcmp(op, "brake") == 0 && argc >= arg_index + 1) {
        return motor_build_brake(config, parse_u16(argv[arg_index]) != 0, packet);
    }
    if (strcmp(op, "reset-zero") == 0) {
        return motor_build_reset_zero_pos(config, packet);
    }
    if (strcmp(op, "get-param") == 0 && argc >= arg_index + 1) {
        return motor_build_get_parameter(config, (MotorParameter) parse_u16(argv[arg_index]),
                                         packet);
    }

    return false;
}

static void print_imu_status(const ExternalImuStatus* imu, uint16_t imu_index) {
    if (imu->has_acceleration) {
        printf("imu[%u] acceleration x=%.3f y=%.3f z=%.3f m/s^2\n", imu_index, imu->acceleration.x,
               imu->acceleration.y, imu->acceleration.z);
    }
    if (imu->has_angular_velocity) {
        printf("imu[%u] angular velocity x=%.3f y=%.3f z=%.3f dps\n", imu_index,
               imu->angular_velocity.x, imu->angular_velocity.y, imu->angular_velocity.z);
    }
    if (imu->has_euler_angle) {
        printf("imu[%u] euler pitch=%.3f roll=%.3f heading=%.3f deg\n", imu_index,
               imu->euler_angle.x, imu->euler_angle.y, imu->euler_angle.z);
    }
    if (imu->has_quaternion) {
        printf("imu[%u] quaternion qw=%.6f qx=%.6f qy=%.6f qz=%.6f\n", imu_index,
               imu->quaternion.qw, imu->quaternion.qx, imu->quaternion.qy, imu->quaternion.qz);
    }
}

/**
 * @brief 程序入口
 * @param[in] argc 参数个数
 * @param[in] argv 参数数组
 * @return 成功返回 0，参数错误返回 2，运行时错误返回 1
 */
int main(int argc, char** argv) {
    // 默认关闭：仅在 EC 转 CAN 型号及固件支持 CAN 时序 SDO 时改为 true。
    const bool enableAdapterConfig = false;
    // 请按自己的 EC 转 CAN、接线和设备参数修改下表；不是所有型号或旧版固件都支持。
    const EcAdapterConfig adapterConfigs[] = {
        {.slave_id = 0,
         .bus_id = 0,
         .bitrate = 1000000,
         .dbitrate = 5000000,
         .sample_point = 0.75,
         .dsample_point = 0.875},
    };
    const size_t adapterConfigCount =
        enableAdapterConfig ? sizeof(adapterConfigs) / sizeof(adapterConfigs[0]) : 0;
    const EcAdapterConfig* activeAdapterConfigs = enableAdapterConfig ? adapterConfigs : NULL;
    EcMasterOptions options = {.cycle_period_us = kDemoCyclePeriodUs};
    int op_index;
    int priority = -1;
    if (!parse_master_options(argc, argv, &op_index, &options, &priority)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc - op_index >= 3 && strcmp(argv[op_index], "imu-monitor") == 0) {
        const uint16_t slave_id = parse_u16(argv[op_index + 1]);
        const uint16_t bus_id = parse_u16(argv[op_index + 2]);
        const uint16_t imu_index = argc - op_index >= 4 ? parse_u16(argv[op_index + 3]) : 0;
        if (imu_index >= EXTERNAL_DEVICE_MAX_INDEX) {
            print_usage(argv[0]);
            return 2;
        }
        EcMaster master = {0};
        if (!ec_master_open_with_configs(&master, argv[1], activeAdapterConfigs, adapterConfigCount,
                                         &options)) {
            return 1;
        }
        if (slave_id >= master.layout.slave_count ||
            bus_id >= master.layout.slaves[slave_id].bus_count) {
            ec_master_close(&master);
            print_usage(argv[0]);
            return 2;
        }
        bool communication_ready = false;
        ExternalImuStatus previous = {0};
        if (priority > 0 && !set_dc_priority(priority)) {
            ec_master_close(&master);
            return 1;
        }
        struct timespec next_cycle;
        clock_gettime(CLOCK_MONOTONIC, &next_cycle);
        for (int i = 0; i < kDemoCycleCount; ++i) {
            if (ec_master_cycle(&master)) {
                communication_ready = true;
            }
            const ExternalImuStatus* current =
                &master.external_devices[slave_id][bus_id].imus[imu_index];
            if (memcmp(&previous, current, sizeof(previous)) != 0) {
                print_imu_status(current, imu_index);
                previous = *current;
            }
            demo_wait_until_next_cycle(&next_cycle, options.cycle_period_us);
        }
        ec_master_close(&master);
        if (!communication_ready) {
            if (enableAdapterConfig) {
                fprintf(stderr,
                        "CAN timing SDO may have been rejected. Check the EC-to-CAN model "
                        "and firmware version; if too old or unsupported, set "
                        "enableAdapterConfig = false in examples/main.c and rebuild.\n");
            }
            fprintf(
                stderr,
                "No valid EtherCAT communication; check EtherCAT state and master diagnostics.\n");
            return 1;
        }
        return 0;
    }
    if (argc - op_index < 6) {
        print_usage(argv[0]);
        return 2;
    }

    const char* master_index = argv[1];
    const char* op = argv[op_index];
    MotorConfig config = motor_config_make(
        parse_u16(argv[op_index + 1]), parse_u16(argv[op_index + 2]), parse_u16(argv[op_index + 4]),
        (MotorModel) parse_u16(argv[op_index + 5]));
    const uint16_t slot = parse_u16(argv[op_index + 3]);
    int arg_index = op_index + 6;
    if (!parse_frame_options(argc, argv, &arg_index, &config.flag)) {
        print_usage(argv[0]);
        return 2;
    }

    MotorPackMsg packet = {0};
    if (!build_operation(argc, argv, arg_index, op, &config, &packet)) {
        print_usage(argv[0]);
        return 2;
    }

    printf("tx slaveId=%u busId=%u slot=%u motorId=%u flag=0x%02x len=%u\n", config.slaveId,
           config.busId, slot, config.motorId, config.flag, packet.len);

    EcMaster master;
    if (!ec_master_open_with_configs(&master, master_index, activeAdapterConfigs,
                                     adapterConfigCount, &options)) {
        return 1;
    }

    if (!ec_validate_target(&master.layout, config.slaveId, config.busId, slot)) {
        ec_master_close(&master);
        return 1;
    }

    if (priority > 0 && !set_dc_priority(priority)) {
        ec_master_close(&master);
        return 1;
    }
    struct timespec next_cycle;
    clock_gettime(CLOCK_MONOTONIC, &next_cycle);
    bool packet_sent = false;
    for (int i = 0; i < kDemoCycleCount; ++i) {
        if (!packet_sent && master.operational) {
            packet_sent = ec_master_send_packet(&master, &config, slot, &packet);
        }
        (void) ec_master_cycle(&master);
        demo_wait_until_next_cycle(&next_cycle, options.cycle_period_us);
    }

    ec_master_close(&master);
    if (!packet_sent) {
        if (enableAdapterConfig) {
            fprintf(stderr,
                    "CAN timing SDO may have been rejected. Check the EC-to-CAN model "
                    "and firmware version; if too old or unsupported, set "
                    "enableAdapterConfig = false in examples/main.c and rebuild.\n");
        }
        fprintf(stderr, "Motor command not sent; check EtherCAT state and master diagnostics.\n");
        return 1;
    }
    return 0;
}
