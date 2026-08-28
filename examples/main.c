#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "example_soem/demo_timing.h"
#include "example_soem/ethercat_layer.h"

static const long kDemoCyclePeriodUs = 1000;
static const int kDemoCycleCount = 5000;

/**
 * @brief 打印程序用法信息
 * @param[in] program 程序名
 */
static void print_usage(const char* program) {
    fprintf(stderr,
            "Usage:\n"
            "  %s <ifname> <op> <slaveId> <busId> <slot> <motorId> <model> "
            "[--eff] [--canfd] [--flag <value>] [args...]\n"
            "\n"
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
            "  sudo %s eth0 spd 0 0 0 1 2 1.0 2.0 1\n",
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
    if (argc >= 5 && strcmp(argv[2], "imu-monitor") == 0) {
        const uint16_t slave_id = parse_u16(argv[3]);
        const uint16_t bus_id = parse_u16(argv[4]);
        const uint16_t imu_index = argc >= 6 ? parse_u16(argv[5]) : 0;
        if (imu_index >= EXTERNAL_DEVICE_MAX_INDEX) {
            print_usage(argv[0]);
            return 2;
        }
        EcMaster master = {0};
        if (!ec_master_open(&master, argv[1])) {
            return 1;
        }
        if (slave_id >= master.layout.slave_count ||
            bus_id >= master.layout.slaves[slave_id].bus_count) {
            ec_master_close(&master);
            print_usage(argv[0]);
            return 2;
        }
        ExternalImuStatus previous = {0};
        struct timespec next_cycle;
        clock_gettime(CLOCK_MONOTONIC, &next_cycle);
        for (int i = 0; i < kDemoCycleCount; ++i) {
            (void) ec_master_cycle(&master);
            const ExternalImuStatus* current =
                &master.external_devices[slave_id][bus_id].imus[imu_index];
            if (memcmp(&previous, current, sizeof(previous)) != 0) {
                print_imu_status(current, imu_index);
                previous = *current;
            }
            demo_wait_until_next_cycle(&next_cycle, kDemoCyclePeriodUs);
        }
        ec_master_close(&master);
        return 0;
    }
    if (argc < 8) {
        print_usage(argv[0]);
        return 2;
    }

    const char* ifname = argv[1];
    const char* op = argv[2];
    MotorConfig config = motor_config_make(parse_u16(argv[3]), parse_u16(argv[4]),
                                           parse_u16(argv[6]), (MotorModel) parse_u16(argv[7]));
    const uint16_t slot = parse_u16(argv[5]);
    int arg_index = 8;
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
    if (!ec_master_open(&master, ifname)) {
        return 1;
    }

    if (!ec_master_send_packet(&master, &config, slot, &packet)) {
        ec_master_close(&master);
        return 1;
    }

    struct timespec next_cycle;
    clock_gettime(CLOCK_MONOTONIC, &next_cycle);
    for (int i = 0; i < kDemoCycleCount; ++i) {
        (void) ec_master_cycle(&master);
        demo_wait_until_next_cycle(&next_cycle, kDemoCyclePeriodUs);
    }

    ec_master_close(&master);
    return 0;
}
