#include "example_igh/external_device.h"
static uint16_t u16(const uint8_t* d) {
    return (uint16_t) (d[0] | ((uint16_t) d[1] << 8));
}
static uint32_t bits(const uint8_t* d, uint8_t s) {
    uint32_t r = 0;
    for (uint8_t b = 0; b < 20; b++)
        if (d[(s + b) / 8] & (1u << ((s + b) % 8)))
            r |= 1u << b;
    return r;
}
static ExternalDeviceFrame battery(ExternalDeviceState* s, const MotorPackMsg* p, uint16_t i,
                                   uint32_t b) {
    ExternalBatteryStatus* x = &s->batteries[i];
    if (b == 0x3F4) {
        if (p->len < 8)
            return EXTERNAL_DEVICE_FRAME_IGNORE;
        x->state =
            (ExternalBatteryState){p->data[0] != 0, p->data[1] / 100.0f, u16(p->data + 2) * .1f,
                                   u16(p->data + 4) * .01f, u16(p->data + 6) * .01f};
        x->has_state = true;
    } else if (b == 0x2F4) {
        if (p->len < 8)
            return EXTERNAL_DEVICE_FRAME_IGNORE;
        x->temperature =
            (ExternalBatteryTemperature){(int16_t) u16(p->data), (int16_t) u16(p->data + 2),
                                         u16(p->data + 4) * .01f, u16(p->data + 6) * .01f};
        x->has_temperature = true;
    } else if (b == 0x0F4) {
        if (p->len < 2)
            return EXTERNAL_DEVICE_FRAME_IGNORE;
        uint8_t l = p->data[0], h = p->data[1];
        x->error = (ExternalBatteryError){l & 1,  l & 2,   l & 4, l & 8, l & 16, l & 32,
                                          l & 64, l & 128, h & 1, h & 2, h & 4};
        x->has_error = true;
    } else {
        if (p->len < 1)
            return EXTERNAL_DEVICE_FRAME_IGNORE;
        uint8_t f = p->data[0];
        x->active_commands =
            (ExternalBatteryActiveCommands){f & 1, f & 2, f & 4, f & 8, f & 16, f & 32};
        x->has_active_commands = true;
    }
    return EXTERNAL_DEVICE_FRAME_BATTERY;
}
static ExternalDeviceFrame imu(ExternalDeviceState* s, const MotorPackMsg* p, uint16_t i) {
    ExternalImuStatus* x = &s->imus[i];
    uint32_t t = p->id & 0x1FFFFF00U;
    if (t == 0x0CF02D00U) {
        if (p->len < 6)
            return EXTERNAL_DEVICE_FRAME_IGNORE;
        x->acceleration =
            (ExternalImuVector3){u16(p->data) * .01f - 320, u16(p->data + 2) * .01f - 320,
                                 u16(p->data + 4) * .01f - 320};
        x->has_acceleration = true;
    } else if (t == 0x0CF02A00U) {
        if (p->len < 8)
            return EXTERNAL_DEVICE_FRAME_IGNORE;
        x->angular_velocity = (ExternalImuVector3){bits(p->data, 0) * .0078125f - 4000,
                                                   bits(p->data, 20) * .0078125f - 4000,
                                                   bits(p->data, 40) * .0078125f - 4000};
        x->has_angular_velocity = true;
    } else if (t == 0x0CF02900U) {
        if (p->len < 6)
            return EXTERNAL_DEVICE_FRAME_IGNORE;
        x->euler_angle =
            (ExternalImuVector3){u16(p->data) * .0078125f - 250, u16(p->data + 2) * .0078125f - 250,
                                 u16(p->data + 4) * .0078125f - 250};
        x->has_euler_angle = true;
    } else if (t == 0x0CF03000U) {
        if (p->len < 8)
            return EXTERNAL_DEVICE_FRAME_IGNORE;
        x->quaternion = (ExternalImuQuaternion){
            u16(p->data) * .000030519f - 1, u16(p->data + 2) * .000030519f - 1,
            u16(p->data + 4) * .000030519f - 1, u16(p->data + 6) * .000030519f - 1};
        x->has_quaternion = true;
    } else
        return EXTERNAL_DEVICE_FRAME_IGNORE;
    return EXTERNAL_DEVICE_FRAME_IMU;
}
ExternalDeviceFrame external_device_process_packet(ExternalDeviceState* s, const MotorPackMsg* p) {
    if (!s || !p || !p->len || p->len > 8)
        return EXTERNAL_DEVICE_FRAME_IGNORE;
    if (p->frame_flags & MOTOR_CAN_FLAG_EFF) {
        uint32_t a = p->id & 255;
        if (a < 0x59 || a >= 0x59 + EXTERNAL_DEVICE_MAX_INDEX)
            return EXTERNAL_DEVICE_FRAME_IGNORE;
        return imu(s, p, a - 0x59);
    }
    uint32_t b[] = {0x3F4, 0x2F4, 0x0F4, 0x1F4};
    for (uint8_t n = 0; n < 4; n++)
        if (p->id >= b[n] && p->id < b[n] + EXTERNAL_DEVICE_MAX_INDEX)
            return battery(s, p, p->id - b[n], b[n]);
    return EXTERNAL_DEVICE_FRAME_MOTOR;
}
