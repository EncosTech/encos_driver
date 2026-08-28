// Auto-generated from motor_models.csv. CSV SHA1: 9c5f88a3c8688225cf1c588e6e70d83e3c1ce298
#include "motor/motor_model_generated.h"

#include <stdexcept>

#include "motor/types.h"

namespace encos {

MotorPVTRanges GetMotorModelRanges(MotorModel model) {
    MotorPVTRanges ranges{};
    switch (model) {
        case MotorModel::EC_A2806_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-12.0f, 12.0f};
            ranges.current = {-10.0f, 10.0f};
            ranges.kt = 1.35f;
            break;
        case MotorModel::EC_A2806_P2_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-12.0f, 12.0f};
            ranges.current = {-10.0f, 10.0f};
            ranges.kt = 1.35f;
            break;
        case MotorModel::EC_A4310_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-30.0f, 30.0f};
            ranges.current = {-30.0f, 30.0f};
            ranges.kt = 1.4f;
            break;
        case MotorModel::EC_A4310_P2_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-30.0f, 30.0f};
            ranges.current = {-30.0f, 30.0f};
            ranges.kt = 1.4f;
            break;
        case MotorModel::EC_A4315_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-70.0f, 70.0f};
            ranges.current = {-30.0f, 30.0f};
            ranges.kt = 2.8f;
            break;
        case MotorModel::EC_A4315_P2_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-70.0f, 70.0f};
            ranges.current = {-30.0f, 30.0f};
            ranges.kt = 2.8f;
            break;
        case MotorModel::EC_A6408_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-60.0f, 60.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.35f;
            break;
        case MotorModel::EC_A6408_P2_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-60.0f, 60.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.35f;
            break;
        case MotorModel::EC_A6416_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-120.0f, 120.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.74f;
            break;
        case MotorModel::EC_A6416_P2_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-120.0f, 120.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.74f;
            break;
        case MotorModel::EC_A8112_P1:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-90.0f, 90.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.1f;
            break;
        case MotorModel::EC_A8112_P1_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-90.0f, 90.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.1f;
            break;
        case MotorModel::EC_A8116_P1:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-150.0f, 150.0f};
            ranges.current = {-70.0f, 70.0f};
            ranges.kt = 2.35f;
            break;
        case MotorModel::EC_A8116_P1_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-150.0f, 150.0f};
            ranges.current = {-70.0f, 70.0f};
            ranges.kt = 2.35f;
            break;
        case MotorModel::EC_A10020_P1_12:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-150.0f, 150.0f};
            ranges.current = {-70.0f, 70.0f};
            ranges.kt = 2.5f;
            break;
        case MotorModel::EC_A10020_P1_12_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-150.0f, 150.0f};
            ranges.current = {-70.0f, 70.0f};
            ranges.kt = 2.5f;
            break;
        case MotorModel::EC_A10020_P1_6:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-150.0f, 150.0f};
            ranges.current = {-70.0f, 70.0f};
            ranges.kt = 1.7f;
            break;
        case MotorModel::EC_A10020_P1_6_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-150.0f, 150.0f};
            ranges.current = {-70.0f, 70.0f};
            ranges.kt = 1.7f;
            break;
        case MotorModel::EC_A13720_P1:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-400.0f, 400.0f};
            ranges.current = {-300.0f, 300.0f};
            ranges.kt = 1.65f;
            break;
        case MotorModel::EC_A13720_P1_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-400.0f, 400.0f};
            ranges.current = {-300.0f, 300.0f};
            ranges.kt = 1.65f;
            break;
        case MotorModel::EC_A13715_P1:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-320.0f, 320.0f};
            ranges.current = {-220.0f, 220.0f};
            ranges.kt = 2.5f;
            break;
        case MotorModel::EC_A13715_P1_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-320.0f, 320.0f};
            ranges.current = {-220.0f, 220.0f};
            ranges.kt = 2.5f;
            break;
        case MotorModel::EC_A4310_P2_H:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-30.0f, 30.0f};
            ranges.current = {-30.0f, 30.0f};
            ranges.kt = 1.4f;
            break;
        case MotorModel::EC_A4310_P2_H_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-30.0f, 30.0f};
            ranges.current = {-30.0f, 30.0f};
            ranges.kt = 1.4f;
            break;
        case MotorModel::EC_A6408_P2_16H:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-45.0f, 45.0f};
            ranges.current = {-30.0f, 30.0f};
            ranges.kt = 2.15f;
            break;
        case MotorModel::EC_A6408_P2_16H_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-45.0f, 45.0f};
            ranges.current = {-30.0f, 30.0f};
            ranges.kt = 2.15f;
            break;
        case MotorModel::EC_A6408_P2_32H:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-60.0f, 60.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.45f;
            break;
        case MotorModel::EC_A6408_P2_32H_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-60.0f, 60.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.45f;
            break;
        case MotorModel::EC_A6408_P2_32HB:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-60.0f, 60.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.2f;
            break;
        case MotorModel::EC_A6408_P2_32HB_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-60.0f, 60.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.2f;
            break;
        case MotorModel::EC_A6416_P2_H:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-120.0f, 120.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.65f;
            break;
        case MotorModel::EC_A6416_P2_H_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-120.0f, 120.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.65f;
            break;
        case MotorModel::EC_A8112_P1_H:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-90.0f, 90.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.1f;
            break;
        case MotorModel::EC_A8112_P1_H_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-90.0f, 90.0f};
            ranges.current = {-60.0f, 60.0f};
            ranges.kt = 2.1f;
            break;
        case MotorModel::EC_A8116_P1_H:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-130.0f, 130.0f};
            ranges.current = {-70.0f, 70.0f};
            ranges.kt = 2.35f;
            break;
        case MotorModel::EC_A8116_P1_H_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-130.0f, 130.0f};
            ranges.current = {-70.0f, 70.0f};
            ranges.kt = 2.35f;
            break;
        case MotorModel::EC_A10010_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-300.0f, 300.0f};
            ranges.current = {-70.0f, 70.0f};
            ranges.kt = 2.6f;
            break;
        case MotorModel::EC_A10010_P2_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-300.0f, 300.0f};
            ranges.current = {-70.0f, 70.0f};
            ranges.kt = 2.6f;
            break;
        case MotorModel::EC_A10020_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-300.0f, 300.0f};
            ranges.current = {-140.0f, 140.0f};
            ranges.kt = 2.6f;
            break;
        case MotorModel::EC_A10020_P2_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-300.0f, 300.0f};
            ranges.current = {-140.0f, 140.0f};
            ranges.kt = 2.6f;
            break;
        case MotorModel::EC_A3814_H14:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-60.0f, 60.0f};
            ranges.current = {-20.0f, 20.0f};
            ranges.kt = 4.2f;
            break;
        case MotorModel::EC_A3814_H14_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-60.0f, 60.0f};
            ranges.current = {-20.0f, 20.0f};
            ranges.kt = 4.2f;
            break;
        case MotorModel::EC_A5013_H17:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-90.0f, 90.0f};
            ranges.current = {-30.0f, 30.0f};
            ranges.kt = 5.9f;
            break;
        case MotorModel::EC_A5013_H17_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-90.0f, 90.0f};
            ranges.current = {-30.0f, 30.0f};
            ranges.kt = 5.9f;
            break;
        case MotorModel::EC_A6013_H20:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-18.0f, 18.0f};
            ranges.torque = {-130.0f, 130.0f};
            ranges.current = {-35.0f, 35.0f};
            ranges.kt = 5.6f;
            break;
        case MotorModel::EC_A6013_H20_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 5.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-130.0f, 130.0f};
            ranges.current = {-35.0f, 35.0f};
            ranges.kt = 5.6f;
            break;
        case MotorModel::EC_A10320_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-360.0f, 360.0f};
            ranges.current = {-160.0f, 160.0f};
            ranges.kt = 2.0f;
            break;
        case MotorModel::EC_A10320_P2_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-360.0f, 360.0f};
            ranges.current = {-160.0f, 160.0f};
            ranges.kt = 2.0f;
            break;
        case MotorModel::EC_A7520_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-130.0f, 130.0f};
            ranges.current = {-70.0f, 70.0f};
            ranges.kt = 2.0f;
            break;
        case MotorModel::EC_A7520_P2_72V:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-130.0f, 130.0f};
            ranges.current = {-70.0f, 70.0f};
            ranges.kt = 2.0f;
            break;
        case MotorModel::EC_A5020_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-42.0f, 42.0f};
            ranges.current = {-40.0f, 40.0f};
            ranges.kt = 1.4f;
            break;
        case MotorModel::EC_A5025_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-50.0f, 50.0f};
            ranges.current = {-40.0f, 40.0f};
            ranges.kt = 1.4f;
            break;
        case MotorModel::EC_A7216_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-140.0f, 140.0f};
            ranges.current = {-100.0f, 100.0f};
            ranges.kt = 1.9f;
            break;
        case MotorModel::EC_A7220_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-160.0f, 160.0f};
            ranges.current = {-100.0f, 100.0f};
            ranges.kt = 1.9f;
            break;
        case MotorModel::EC_A7225_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-200.0f, 200.0f};
            ranges.current = {-100.0f, 100.0f};
            ranges.kt = 2.0f;
            break;
        case MotorModel::EC_A9016_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-240.0f, 240.0f};
            ranges.current = {-160.0f, 160.0f};
            ranges.kt = 1.95f;
            break;
        case MotorModel::EC_A9020_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-280.0f, 280.0f};
            ranges.current = {-160.0f, 160.0f};
            ranges.kt = 1.9f;
            break;
        case MotorModel::EC_A9025_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-320.0f, 320.0f};
            ranges.current = {-160.0f, 160.0f};
            ranges.kt = 2.1f;
            break;
        case MotorModel::EC_A10820_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-400.0f, 400.0f};
            ranges.current = {-220.0f, 220.0f};
            ranges.kt = 2.0f;
            break;
        case MotorModel::EC_A10825_P2:
            ranges.kp = {0.0f, 500.0f};
            ranges.kd = {0.0f, 50.0f};
            ranges.position = {-12.5f, 12.5f};
            ranges.speed = {-30.0f, 30.0f};
            ranges.torque = {-450.0f, 450.0f};
            ranges.current = {-220.0f, 220.0f};
            ranges.kt = 2.0f;
            break;
    }
    return ranges;
}

MotorModel StringToMotorModel(const std::string& str) {
    if (str == "EC_A2806_P2") {
        return MotorModel::EC_A2806_P2;
    }
    if (str == "EC_A2806_P2_72V") {
        return MotorModel::EC_A2806_P2_72V;
    }
    if (str == "EC_A4310_P2") {
        return MotorModel::EC_A4310_P2;
    }
    if (str == "EC_A4310_P2_72V") {
        return MotorModel::EC_A4310_P2_72V;
    }
    if (str == "EC_A4315_P2") {
        return MotorModel::EC_A4315_P2;
    }
    if (str == "EC_A4315_P2_72V") {
        return MotorModel::EC_A4315_P2_72V;
    }
    if (str == "EC_A6408_P2") {
        return MotorModel::EC_A6408_P2;
    }
    if (str == "EC_A6408_P2_72V") {
        return MotorModel::EC_A6408_P2_72V;
    }
    if (str == "EC_A6416_P2") {
        return MotorModel::EC_A6416_P2;
    }
    if (str == "EC_A6416_P2_72V") {
        return MotorModel::EC_A6416_P2_72V;
    }
    if (str == "EC_A8112_P1") {
        return MotorModel::EC_A8112_P1;
    }
    if (str == "EC_A8112_P1_72V") {
        return MotorModel::EC_A8112_P1_72V;
    }
    if (str == "EC_A8116_P1") {
        return MotorModel::EC_A8116_P1;
    }
    if (str == "EC_A8116_P1_72V") {
        return MotorModel::EC_A8116_P1_72V;
    }
    if (str == "EC_A10020_P1_12") {
        return MotorModel::EC_A10020_P1_12;
    }
    if (str == "EC_A10020_P1_12_72V") {
        return MotorModel::EC_A10020_P1_12_72V;
    }
    if (str == "EC_A10020_P1_6") {
        return MotorModel::EC_A10020_P1_6;
    }
    if (str == "EC_A10020_P1_6_72V") {
        return MotorModel::EC_A10020_P1_6_72V;
    }
    if (str == "EC_A13720_P1") {
        return MotorModel::EC_A13720_P1;
    }
    if (str == "EC_A13720_P1_72V") {
        return MotorModel::EC_A13720_P1_72V;
    }
    if (str == "EC_A13715_P1") {
        return MotorModel::EC_A13715_P1;
    }
    if (str == "EC_A13715_P1_72V") {
        return MotorModel::EC_A13715_P1_72V;
    }
    if (str == "EC_A4310_P2_H") {
        return MotorModel::EC_A4310_P2_H;
    }
    if (str == "EC_A4310_P2_H_72V") {
        return MotorModel::EC_A4310_P2_H_72V;
    }
    if (str == "EC_A6408_P2_16H") {
        return MotorModel::EC_A6408_P2_16H;
    }
    if (str == "EC_A6408_P2_16H_72V") {
        return MotorModel::EC_A6408_P2_16H_72V;
    }
    if (str == "EC_A6408_P2_32H") {
        return MotorModel::EC_A6408_P2_32H;
    }
    if (str == "EC_A6408_P2_32H_72V") {
        return MotorModel::EC_A6408_P2_32H_72V;
    }
    if (str == "EC_A6408_P2_32HB") {
        return MotorModel::EC_A6408_P2_32HB;
    }
    if (str == "EC_A6408_P2_32HB_72V") {
        return MotorModel::EC_A6408_P2_32HB_72V;
    }
    if (str == "EC_A6416_P2_H") {
        return MotorModel::EC_A6416_P2_H;
    }
    if (str == "EC_A6416_P2_H_72V") {
        return MotorModel::EC_A6416_P2_H_72V;
    }
    if (str == "EC_A8112_P1_H") {
        return MotorModel::EC_A8112_P1_H;
    }
    if (str == "EC_A8112_P1_H_72V") {
        return MotorModel::EC_A8112_P1_H_72V;
    }
    if (str == "EC_A8116_P1_H") {
        return MotorModel::EC_A8116_P1_H;
    }
    if (str == "EC_A8116_P1_H_72V") {
        return MotorModel::EC_A8116_P1_H_72V;
    }
    if (str == "EC_A10010_P2") {
        return MotorModel::EC_A10010_P2;
    }
    if (str == "EC_A10010_P2_72V") {
        return MotorModel::EC_A10010_P2_72V;
    }
    if (str == "EC_A10020_P2") {
        return MotorModel::EC_A10020_P2;
    }
    if (str == "EC_A10020_P2_72V") {
        return MotorModel::EC_A10020_P2_72V;
    }
    if (str == "EC_A3814_H14") {
        return MotorModel::EC_A3814_H14;
    }
    if (str == "EC_A3814_H14_72V") {
        return MotorModel::EC_A3814_H14_72V;
    }
    if (str == "EC_A5013_H17") {
        return MotorModel::EC_A5013_H17;
    }
    if (str == "EC_A5013_H17_72V") {
        return MotorModel::EC_A5013_H17_72V;
    }
    if (str == "EC_A6013_H20") {
        return MotorModel::EC_A6013_H20;
    }
    if (str == "EC_A6013_H20_72V") {
        return MotorModel::EC_A6013_H20_72V;
    }
    if (str == "EC_A10320_P2") {
        return MotorModel::EC_A10320_P2;
    }
    if (str == "EC_A10320_P2_72V") {
        return MotorModel::EC_A10320_P2_72V;
    }
    if (str == "EC_A7520_P2") {
        return MotorModel::EC_A7520_P2;
    }
    if (str == "EC_A7520_P2_72V") {
        return MotorModel::EC_A7520_P2_72V;
    }
    if (str == "EC_A5020_P2") {
        return MotorModel::EC_A5020_P2;
    }
    if (str == "EC_A5025_P2") {
        return MotorModel::EC_A5025_P2;
    }
    if (str == "EC_A7216_P2") {
        return MotorModel::EC_A7216_P2;
    }
    if (str == "EC_A7220_P2") {
        return MotorModel::EC_A7220_P2;
    }
    if (str == "EC_A7225_P2") {
        return MotorModel::EC_A7225_P2;
    }
    if (str == "EC_A9016_P2") {
        return MotorModel::EC_A9016_P2;
    }
    if (str == "EC_A9020_P2") {
        return MotorModel::EC_A9020_P2;
    }
    if (str == "EC_A9025_P2") {
        return MotorModel::EC_A9025_P2;
    }
    if (str == "EC_A10820_P2") {
        return MotorModel::EC_A10820_P2;
    }
    if (str == "EC_A10825_P2") {
        return MotorModel::EC_A10825_P2;
    }
    throw std::invalid_argument("Unknown motor model: " + str);
}

const char* MotorModelToString(MotorModel model) {
    switch (model) {
        case MotorModel::EC_A2806_P2:
            return "EC_A2806_P2";
        case MotorModel::EC_A2806_P2_72V:
            return "EC_A2806_P2_72V";
        case MotorModel::EC_A4310_P2:
            return "EC_A4310_P2";
        case MotorModel::EC_A4310_P2_72V:
            return "EC_A4310_P2_72V";
        case MotorModel::EC_A4315_P2:
            return "EC_A4315_P2";
        case MotorModel::EC_A4315_P2_72V:
            return "EC_A4315_P2_72V";
        case MotorModel::EC_A6408_P2:
            return "EC_A6408_P2";
        case MotorModel::EC_A6408_P2_72V:
            return "EC_A6408_P2_72V";
        case MotorModel::EC_A6416_P2:
            return "EC_A6416_P2";
        case MotorModel::EC_A6416_P2_72V:
            return "EC_A6416_P2_72V";
        case MotorModel::EC_A8112_P1:
            return "EC_A8112_P1";
        case MotorModel::EC_A8112_P1_72V:
            return "EC_A8112_P1_72V";
        case MotorModel::EC_A8116_P1:
            return "EC_A8116_P1";
        case MotorModel::EC_A8116_P1_72V:
            return "EC_A8116_P1_72V";
        case MotorModel::EC_A10020_P1_12:
            return "EC_A10020_P1_12";
        case MotorModel::EC_A10020_P1_12_72V:
            return "EC_A10020_P1_12_72V";
        case MotorModel::EC_A10020_P1_6:
            return "EC_A10020_P1_6";
        case MotorModel::EC_A10020_P1_6_72V:
            return "EC_A10020_P1_6_72V";
        case MotorModel::EC_A13720_P1:
            return "EC_A13720_P1";
        case MotorModel::EC_A13720_P1_72V:
            return "EC_A13720_P1_72V";
        case MotorModel::EC_A13715_P1:
            return "EC_A13715_P1";
        case MotorModel::EC_A13715_P1_72V:
            return "EC_A13715_P1_72V";
        case MotorModel::EC_A4310_P2_H:
            return "EC_A4310_P2_H";
        case MotorModel::EC_A4310_P2_H_72V:
            return "EC_A4310_P2_H_72V";
        case MotorModel::EC_A6408_P2_16H:
            return "EC_A6408_P2_16H";
        case MotorModel::EC_A6408_P2_16H_72V:
            return "EC_A6408_P2_16H_72V";
        case MotorModel::EC_A6408_P2_32H:
            return "EC_A6408_P2_32H";
        case MotorModel::EC_A6408_P2_32H_72V:
            return "EC_A6408_P2_32H_72V";
        case MotorModel::EC_A6408_P2_32HB:
            return "EC_A6408_P2_32HB";
        case MotorModel::EC_A6408_P2_32HB_72V:
            return "EC_A6408_P2_32HB_72V";
        case MotorModel::EC_A6416_P2_H:
            return "EC_A6416_P2_H";
        case MotorModel::EC_A6416_P2_H_72V:
            return "EC_A6416_P2_H_72V";
        case MotorModel::EC_A8112_P1_H:
            return "EC_A8112_P1_H";
        case MotorModel::EC_A8112_P1_H_72V:
            return "EC_A8112_P1_H_72V";
        case MotorModel::EC_A8116_P1_H:
            return "EC_A8116_P1_H";
        case MotorModel::EC_A8116_P1_H_72V:
            return "EC_A8116_P1_H_72V";
        case MotorModel::EC_A10010_P2:
            return "EC_A10010_P2";
        case MotorModel::EC_A10010_P2_72V:
            return "EC_A10010_P2_72V";
        case MotorModel::EC_A10020_P2:
            return "EC_A10020_P2";
        case MotorModel::EC_A10020_P2_72V:
            return "EC_A10020_P2_72V";
        case MotorModel::EC_A3814_H14:
            return "EC_A3814_H14";
        case MotorModel::EC_A3814_H14_72V:
            return "EC_A3814_H14_72V";
        case MotorModel::EC_A5013_H17:
            return "EC_A5013_H17";
        case MotorModel::EC_A5013_H17_72V:
            return "EC_A5013_H17_72V";
        case MotorModel::EC_A6013_H20:
            return "EC_A6013_H20";
        case MotorModel::EC_A6013_H20_72V:
            return "EC_A6013_H20_72V";
        case MotorModel::EC_A10320_P2:
            return "EC_A10320_P2";
        case MotorModel::EC_A10320_P2_72V:
            return "EC_A10320_P2_72V";
        case MotorModel::EC_A7520_P2:
            return "EC_A7520_P2";
        case MotorModel::EC_A7520_P2_72V:
            return "EC_A7520_P2_72V";
        case MotorModel::EC_A5020_P2:
            return "EC_A5020_P2";
        case MotorModel::EC_A5025_P2:
            return "EC_A5025_P2";
        case MotorModel::EC_A7216_P2:
            return "EC_A7216_P2";
        case MotorModel::EC_A7220_P2:
            return "EC_A7220_P2";
        case MotorModel::EC_A7225_P2:
            return "EC_A7225_P2";
        case MotorModel::EC_A9016_P2:
            return "EC_A9016_P2";
        case MotorModel::EC_A9020_P2:
            return "EC_A9020_P2";
        case MotorModel::EC_A9025_P2:
            return "EC_A9025_P2";
        case MotorModel::EC_A10820_P2:
            return "EC_A10820_P2";
        case MotorModel::EC_A10825_P2:
            return "EC_A10825_P2";
    }
    throw std::invalid_argument("Unknown motor model enum");
}

std::vector<const char*> GetAllMotorModelStrings() {
    // clang-format off
    return {
        "EC_A2806_P2",
        "EC_A2806_P2_72V",
        "EC_A4310_P2",
        "EC_A4310_P2_72V",
        "EC_A4315_P2",
        "EC_A4315_P2_72V",
        "EC_A6408_P2",
        "EC_A6408_P2_72V",
        "EC_A6416_P2",
        "EC_A6416_P2_72V",
        "EC_A8112_P1",
        "EC_A8112_P1_72V",
        "EC_A8116_P1",
        "EC_A8116_P1_72V",
        "EC_A10020_P1_12",
        "EC_A10020_P1_12_72V",
        "EC_A10020_P1_6",
        "EC_A10020_P1_6_72V",
        "EC_A13720_P1",
        "EC_A13720_P1_72V",
        "EC_A13715_P1",
        "EC_A13715_P1_72V",
        "EC_A4310_P2_H",
        "EC_A4310_P2_H_72V",
        "EC_A6408_P2_16H",
        "EC_A6408_P2_16H_72V",
        "EC_A6408_P2_32H",
        "EC_A6408_P2_32H_72V",
        "EC_A6408_P2_32HB",
        "EC_A6408_P2_32HB_72V",
        "EC_A6416_P2_H",
        "EC_A6416_P2_H_72V",
        "EC_A8112_P1_H",
        "EC_A8112_P1_H_72V",
        "EC_A8116_P1_H",
        "EC_A8116_P1_H_72V",
        "EC_A10010_P2",
        "EC_A10010_P2_72V",
        "EC_A10020_P2",
        "EC_A10020_P2_72V",
        "EC_A3814_H14",
        "EC_A3814_H14_72V",
        "EC_A5013_H17",
        "EC_A5013_H17_72V",
        "EC_A6013_H20",
        "EC_A6013_H20_72V",
        "EC_A10320_P2",
        "EC_A10320_P2_72V",
        "EC_A7520_P2",
        "EC_A7520_P2_72V",
        "EC_A5020_P2",
        "EC_A5025_P2",
        "EC_A7216_P2",
        "EC_A7220_P2",
        "EC_A7225_P2",
        "EC_A9016_P2",
        "EC_A9020_P2",
        "EC_A9025_P2",
        "EC_A10820_P2",
        "EC_A10825_P2",
    };
    // clang-format on
}

}  // namespace encos
