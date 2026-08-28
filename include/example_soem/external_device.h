#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "example_soem/motor_layer.h"
#define EXTERNAL_DEVICE_MAX_INDEX 10
typedef enum {
    EXTERNAL_DEVICE_FRAME_MOTOR,
    EXTERNAL_DEVICE_FRAME_IMU,
    EXTERNAL_DEVICE_FRAME_BATTERY,
    EXTERNAL_DEVICE_FRAME_IGNORE
} ExternalDeviceFrame;
typedef struct {
    bool is_master;
    float soc, voltage, allowed_discharge_current, allowed_charge_current;
} ExternalBatteryState;
typedef struct {
    float battery, mos, discharge_current, charge_current;
} ExternalBatteryTemperature;
typedef struct {
    bool could_not_charge, could_not_discharge, low_battery, over_current_steady, over_current_peak,
        over_current_charge, battery_over_temp, mos_over_temp, could_not_communicate,
        stopped_emergency, charger_fault;
} ExternalBatteryError;
typedef struct {
    bool shutdown_request, discharge_request, force_shutdown_broadcast, allow_charging,
        fault_shutdown_broadcast, mos_status;
} ExternalBatteryActiveCommands;
typedef struct {
    bool has_state, has_temperature, has_error, has_active_commands;
    ExternalBatteryState state;
    ExternalBatteryTemperature temperature;
    ExternalBatteryError error;
    ExternalBatteryActiveCommands active_commands;
} ExternalBatteryStatus;
typedef struct {
    float x, y, z;
} ExternalImuVector3;
typedef struct {
    float qw, qx, qy, qz;
} ExternalImuQuaternion;
typedef struct {
    bool has_acceleration, has_angular_velocity, has_euler_angle, has_quaternion;
    ExternalImuVector3 acceleration, angular_velocity, euler_angle;
    ExternalImuQuaternion quaternion;
} ExternalImuStatus;
typedef struct {
    ExternalBatteryStatus batteries[EXTERNAL_DEVICE_MAX_INDEX];
    ExternalImuStatus imus[EXTERNAL_DEVICE_MAX_INDEX];
} ExternalDeviceState;
ExternalDeviceFrame external_device_process_packet(ExternalDeviceState* state,
                                                   const MotorPackMsg* packet);
