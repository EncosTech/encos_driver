#include <stdio.h>
#include <string.h>

#include "example_igh/demo_timing.h"
#include "example_igh/ethercat_layer.h"
#include "example_igh/external_device.h"
#include "example_igh/motor_layer.h"

#define CHECK(expr)                                                                  \
    do {                                                                             \
        if (!(expr)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr); \
            return 1;                                                                \
        }                                                                            \
    } while (0)

static int test_pdo_identification(void) {
    EcSlavePdoLayout layout;

    CHECK(ec_identify_pdo_layout(86, &layout));
    CHECK(layout.format == EC_SLAVE_FORMAT_CLASSIC_CAN_2_BUS);
    CHECK(layout.bus_count == 2);
    CHECK(layout.slots_per_bus == 3);
    CHECK(layout.motor_offset == 2);

    CHECK(ec_identify_pdo_layout(336, &layout));
    CHECK(layout.format == EC_SLAVE_FORMAT_CAN_FD_3_BUS);
    CHECK(layout.bus_count == 3);
    CHECK(layout.slots_per_bus == 8);
    CHECK(layout.motor_offset == 0);

    CHECK(ec_identify_pdo_layout(896, &layout));
    CHECK(layout.format == EC_SLAVE_FORMAT_CAN_FD_8_BUS);
    CHECK(layout.bus_count == 8);
    CHECK(layout.slots_per_bus == 8);

    CHECK(!ec_identify_pdo_layout(340, &layout));
    return 0;
}

static int test_validate_and_offset(void) {
    EcLayoutTable table = {0};
    CHECK(ec_identify_pdo_layout(336, &table.slaves[0]));
    table.slave_count = 1;

    CHECK(ec_validate_target(&table, 0, 2, 7));
    CHECK(!ec_validate_target(&table, 1, 0, 0));
    CHECK(!ec_validate_target(&table, 0, 3, 0));
    CHECK(!ec_validate_target(&table, 0, 0, 8));

    CHECK(ec_motor_slot_offset(&table.slaves[0], 2, 7) == 23 * sizeof(MotorPackMsg));
    return 0;
}

static int test_write_packet_offset(void) {
    EcSlavePdoLayout layout;
    uint8_t output[336] = {0};
    MotorPackMsg packet = {0};
    MotorPackMsg copied = {0};

    CHECK(ec_identify_pdo_layout(sizeof(output), &layout));
    packet.id = 0x123;
    packet.frame_flags = MOTOR_CAN_FLAG_FD_MASK;
    packet.len = 3;
    packet.data[0] = 0x60;
    packet.data[1] = 0x12;
    packet.data[2] = 0x34;

    CHECK(ec_write_motor_packet(output, &layout, 1, 2, &packet));
    memcpy(&copied, output + ec_motor_slot_offset(&layout, 1, 2), sizeof(copied));
    CHECK(copied.id == packet.id);
    CHECK(copied.frame_flags == packet.frame_flags);
    CHECK(copied.len == packet.len);
    CHECK(copied.data[0] == packet.data[0]);
    return 0;
}

static int test_motor_encoding(void) {
    MotorConfig config = {0};
    MotorPackMsg packet = {0};
    config.motorId = 1;
    config.flag = MOTOR_CAN_FLAG_FD_MASK;
    config.model = MOTOR_MODEL_EC_A4310_P2;

    CHECK(motor_build_spd_control(&config, 1.0f, 2.0f, 3, &packet));
    CHECK(packet.id == 1);
    CHECK(packet.frame_flags == MOTOR_CAN_FLAG_FD_MASK);
    CHECK(packet.len == 7);
    CHECK(packet.data[0] == (uint8_t) (0x40 | 3));

    CHECK(motor_build_reset_zero_pos(&config, &packet));
    CHECK(packet.id == 0x7FF);
    CHECK(packet.len == 6);
    CHECK(packet.data[0] == 0);
    CHECK(packet.data[1] == 1);
    CHECK(packet.data[3] == 0x03);
    CHECK(packet.data[4] == 0);
    CHECK(packet.data[5] == 0);

    CHECK(motor_build_get_parameter(&config, MOTOR_PARAM_ACCELERATION, &packet));
    CHECK(packet.id == 1);
    CHECK(packet.len == 2);
    CHECK(packet.data[0] == (uint8_t) (0x07 << 5));
    CHECK(packet.data[1] == MOTOR_PARAM_ACCELERATION);
    return 0;
}

static int test_motor_config_flags(void) {
    const MotorConfig classic = motor_config_make(0, 2, 11, MOTOR_MODEL_EC_A4310_P2);
    const MotorConfig canfd =
        motor_config_make_with_flags(0, 2, 11, MOTOR_MODEL_EC_A4310_P2, true, true);

    CHECK(classic.slaveId == 0);
    CHECK(classic.busId == 2);
    CHECK(classic.motorId == 11);
    CHECK(classic.flag == 0);
    CHECK(canfd.flag == (MOTOR_CAN_FLAG_EFF | MOTOR_CAN_FLAG_FD_MASK));
    return 0;
}

static int test_timespec_add_us_normalizes_nsec(void) {
    struct timespec point = {.tv_sec = 3, .tv_nsec = 999900000L};

    demo_timespec_add_us(&point, 250);

    CHECK(point.tv_sec == 4);
    CHECK(point.tv_nsec == 150000L);
    return 0;
}

static int test_external_device_routing_and_decode(void) {
    ExternalDeviceState state = {0};
    MotorPackMsg packet = {0};

    packet.id = 0x3F4 + 3;
    packet.len = 8;
    packet.data[0] = 1;
    packet.data[1] = 75;
    packet.data[2] = 0xE8;
    packet.data[3] = 0x03;
    packet.data[4] = 0x10;
    packet.data[5] = 0x27;
    packet.data[6] = 0x20;
    packet.data[7] = 0x4E;
    CHECK(external_device_process_packet(&state, &packet) == EXTERNAL_DEVICE_FRAME_BATTERY);
    CHECK(state.batteries[3].has_state);
    CHECK(state.batteries[3].state.is_master);
    CHECK(state.batteries[3].state.soc == 0.75f);
    CHECK(state.batteries[3].state.voltage == 100.0f);
    CHECK(state.batteries[3].state.allowed_discharge_current == 100.0f);
    CHECK(state.batteries[3].state.allowed_charge_current == 200.0f);

    packet = (MotorPackMsg){0};
    packet.id = 0x0CF02D59 + 4;
    packet.frame_flags = MOTOR_CAN_FLAG_EFF;
    packet.len = 6;
    packet.data[0] = 0x00;
    packet.data[1] = 0x7D;
    packet.data[2] = 0x64;
    packet.data[3] = 0x7D;
    packet.data[4] = 0x9C;
    packet.data[5] = 0x7C;
    CHECK(external_device_process_packet(&state, &packet) == EXTERNAL_DEVICE_FRAME_IMU);
    CHECK(state.imus[4].has_acceleration);
    CHECK(state.imus[4].acceleration.x == 0.0f);
    CHECK(state.imus[4].acceleration.y == 1.0f);
    CHECK(state.imus[4].acceleration.z == -1.0f);

    packet = (MotorPackMsg){.id = 0x3F4 + EXTERNAL_DEVICE_MAX_INDEX, .len = 8};
    CHECK(external_device_process_packet(&state, &packet) == EXTERNAL_DEVICE_FRAME_MOTOR);

    packet = (MotorPackMsg){
        .id = 0x0CF02D59 + EXTERNAL_DEVICE_MAX_INDEX, .frame_flags = MOTOR_CAN_FLAG_EFF, .len = 6};
    CHECK(external_device_process_packet(&state, &packet) == EXTERNAL_DEVICE_FRAME_IGNORE);

    packet = (MotorPackMsg){.id = 0x123, .len = 8};
    CHECK(external_device_process_packet(&state, &packet) == EXTERNAL_DEVICE_FRAME_MOTOR);

    packet = (MotorPackMsg){.id = 0x18FF50E5, .frame_flags = MOTOR_CAN_FLAG_EFF, .len = 8};
    CHECK(external_device_process_packet(&state, &packet) == EXTERNAL_DEVICE_FRAME_IGNORE);
    return 0;
}

int main(void) {
    CHECK(sizeof(MotorPackMsg) == 14);
    CHECK(test_pdo_identification() == 0);
    CHECK(test_validate_and_offset() == 0);
    CHECK(test_write_packet_offset() == 0);
    CHECK(test_motor_encoding() == 0);
    CHECK(test_motor_config_flags() == 0);
    CHECK(test_timespec_add_us_normalizes_nsec() == 0);
    CHECK(test_external_device_routing_and_decode() == 0);
    return 0;
}
