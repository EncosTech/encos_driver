#pragma once

/**
 * @file math_utils.h
 * @brief 电机数值转换工具函数
 *
 * 提供浮点数与定点整数之间的转换，用于电机通信协议的数据编码/解码
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将浮点数转换为定点整数
 * @param x 要转换的浮点值
 * @param x_min 浮点值范围下限
 * @param x_max 浮点值范围上限
 * @param bits 目标整数的位数
 * @return 编码后的整数值
 */
int FloatToUint(float x, float x_min, float x_max, int bits);

/**
 * @brief 将定点整数转换为浮点数
 * @param x_int 要转换的整数值
 * @param x_min 浮点值范围下限
 * @param x_max 浮点值范围上限
 * @param bits 整数的位数
 * @return 解码后的浮点值
 */
float UintToFloat(int x_int, float x_min, float x_max, int bits);

#ifdef __cplusplus
}
#endif
