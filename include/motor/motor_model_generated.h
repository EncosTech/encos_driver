// Auto-generated from motor_models.csv. CSV SHA1: 9c5f88a3c8688225cf1c588e6e70d83e3c1ce298
#pragma once

#include <string>
#include <vector>

#include "encos/export.h"

namespace encos {

/** @brief 受支持的电机型号 */
enum class MotorModel {
    EC_A2806_P2,
    EC_A2806_P2_72V,
    EC_A4310_P2,
    EC_A4310_P2_72V,
    EC_A4315_P2,
    EC_A4315_P2_72V,
    EC_A6408_P2,
    EC_A6408_P2_72V,
    EC_A6416_P2,
    EC_A6416_P2_72V,
    EC_A8112_P1,
    EC_A8112_P1_72V,
    EC_A8116_P1,
    EC_A8116_P1_72V,
    EC_A10020_P1_12,
    EC_A10020_P1_12_72V,
    EC_A10020_P1_6,
    EC_A10020_P1_6_72V,
    EC_A13720_P1,
    EC_A13720_P1_72V,
    EC_A13715_P1,
    EC_A13715_P1_72V,
    EC_A4310_P2_H,
    EC_A4310_P2_H_72V,
    EC_A6408_P2_16H,
    EC_A6408_P2_16H_72V,
    EC_A6408_P2_32H,
    EC_A6408_P2_32H_72V,
    EC_A6408_P2_32HB,
    EC_A6408_P2_32HB_72V,
    EC_A6416_P2_H,
    EC_A6416_P2_H_72V,
    EC_A8112_P1_H,
    EC_A8112_P1_H_72V,
    EC_A8116_P1_H,
    EC_A8116_P1_H_72V,
    EC_A10010_P2,
    EC_A10010_P2_72V,
    EC_A10020_P2,
    EC_A10020_P2_72V,
    EC_A3814_H14,
    EC_A3814_H14_72V,
    EC_A5013_H17,
    EC_A5013_H17_72V,
    EC_A6013_H20,
    EC_A6013_H20_72V,
    EC_A10320_P2,
    EC_A10320_P2_72V,
    EC_A7520_P2,
    EC_A7520_P2_72V,
    EC_A5020_P2,
    EC_A5025_P2,
    EC_A7216_P2,
    EC_A7220_P2,
    EC_A7225_P2,
    EC_A9016_P2,
    EC_A9020_P2,
    EC_A9025_P2,
    EC_A10820_P2,
    EC_A10825_P2,
};

struct MotorPVTRanges;
/**
 * @brief 获取指定电机型号的 PVT 控制范围
 * @param model 电机型号
 * @return 该型号的 PVT 控制范围与转矩常数
 */
ENCOS_BASE_API MotorPVTRanges GetMotorModelRanges(MotorModel model);

/** @brief 将型号名称转换为枚举，未知名称会抛出 std::invalid_argument */
ENCOS_BASE_API MotorModel StringToMotorModel(const std::string& str);
/** @brief 将电机型号枚举转换为静态字符串 */
ENCOS_BASE_API const char* MotorModelToString(MotorModel model);
/** @brief 获取全部受支持的电机型号名称 */
ENCOS_BASE_API std::vector<const char*> GetAllMotorModelStrings();

}  // namespace encos
