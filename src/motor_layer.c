#include "example_igh/motor_layer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MOTOR_PI_F 3.14159265358979323846f

/**
 * @brief 将浮点数限制在 [min, max] 范围内
 * @param[in] value 输入值
 * @param[in] min 最小值
 * @param[in] max 最大值
 * @return 限制后的值
 */
static float clamp_float(float value, float min, float max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

/**
 * @brief 将浮点数限制在 [0, 65535] 范围内并转为 uint16_t
 * @param[in] value 输入值
 * @return 限制后的 uint16_t 值
 */
static uint16_t clamp_u16_float(float value) {
    if (value < 0.0f) {
        return 0;
    }
    if (value > 65535.0f) {
        return 65535;
    }
    return (uint16_t) value;
}

/**
 * @brief 将 uint16_t 以大端序写入缓冲区
 * @param[in,out] data 目标缓冲区
 * @param[in] value 要写入的值
 */
static void write_u16_be(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t) (value >> 8);
    data[1] = (uint8_t) (value & 0xFF);
}

/**
 * @brief 将 int16_t 以大端序写入缓冲区
 * @param[in,out] data 目标缓冲区
 * @param[in] value 要写入的值
 */
static void write_i16_be(uint8_t* data, int16_t value) {
    write_u16_be(data, (uint16_t) value);
}

/**
 * @brief 从大端序缓冲区读取 uint16_t
 * @param[in] data 源缓冲区
 * @return 读取到的值
 */
static uint16_t read_u16_be(const uint8_t* data) {
    return (uint16_t) (((uint16_t) data[0] << 8) | data[1]);
}

/**
 * @brief 从大端序缓冲区读取 int16_t
 * @param[in] data 源缓冲区
 * @return 读取到的值
 */
static int16_t read_i16_be(const uint8_t* data) {
    return (int16_t) read_u16_be(data);
}

/**
 * @brief 将 float 以大端序 IEEE-754 格式写入缓冲区
 * @param[in,out] data 目标缓冲区
 * @param[in] value 要写入的浮点值
 */
static void write_float_be(uint8_t* data, float value) {
    uint32_t raw = 0;
    memcpy(&raw, &value, sizeof(raw));
    data[0] = (uint8_t) (raw >> 24);
    data[1] = (uint8_t) (raw >> 16);
    data[2] = (uint8_t) (raw >> 8);
    data[3] = (uint8_t) (raw & 0xFF);
}

/**
 * @brief 从大端序缓冲区读取 float
 * @param[in] data 源缓冲区
 * @return 读取到的浮点值
 */
static float read_float_be(const uint8_t* data) {
    uint32_t raw = ((uint32_t) data[0] << 24) | ((uint32_t) data[1] << 16) |
                   ((uint32_t) data[2] << 8) | data[3];
    float value = 0.0f;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

/**
 * @brief 验证配置和报文指针是否有效，并初始化报文基础字段
 * @param[in] config 电机配置
 * @param[out] packet 报文
 * @return 有效返回 true，无效返回 false
 */
static bool valid_config_packet(const MotorConfig* config, MotorPackMsg* packet) {
    if (config == NULL || packet == NULL || config->model >= MOTOR_MODEL_COUNT) {
        return false;
    }

    memset(packet, 0, sizeof(*packet));
    packet->id = config->motorId;
    packet->frame_flags = motor_sanitize_flags(config->flag);
    return true;
}

/**
 * @brief 验证反馈类型是否有效
 * @param[in] feedback 反馈类型
 * @return 有效返回 true，无效返回 false
 */
static bool valid_feedback(uint8_t feedback) {
    return feedback <= 3;
}

/**
 * @brief 创建电机配置（默认标准帧）
 * @param[in] slave_id 从站编号
 * @param[in] bus_id 总线编号
 * @param[in] motor_id 电机编号
 * @param[in] model 电机型号
 * @return 电机配置结构体
 */
MotorConfig motor_config_make(uint16_t slave_id, uint16_t bus_id, uint16_t motor_id,
                              MotorModel model) {
    return motor_config_make_with_flags(slave_id, bus_id, motor_id, model, false, false);
}

/**
 * @brief 创建电机配置（可指定帧标志）
 * @param[in] slave_id 从站编号
 * @param[in] bus_id 总线编号
 * @param[in] motor_id 电机编号
 * @param[in] model 电机型号
 * @param[in] eff 是否使用扩展帧
 * @param[in] canfd 是否使用 CAN FD
 * @return 电机配置结构体
 */
MotorConfig motor_config_make_with_flags(uint16_t slave_id, uint16_t bus_id, uint16_t motor_id,
                                         MotorModel model, bool eff, bool canfd) {
    uint8_t flags = 0;
    if (eff) {
        flags = (uint8_t) (flags | MOTOR_CAN_FLAG_EFF);
    }
    if (canfd) {
        flags = (uint8_t) (flags | MOTOR_CAN_FLAG_FD_MASK);
    }

    return (MotorConfig){
        .slaveId = slave_id,
        .busId = bus_id,
        .motorId = motor_id,
        .flag = motor_sanitize_flags(flags),
        .model = model,
    };
}

/**
 * @brief 规范化 CAN 帧标志
 * @param[in] flags 原始标志
 * @return 规范化后的标志
 */
uint8_t motor_sanitize_flags(uint8_t flags) {
    return (uint8_t) (flags & MOTOR_CAN_FLAG_MASK);
}

/**
 * @brief 将浮点数映射为无符号整数
 * @param[in] value 浮点数值
 * @param[in] min 最小值
 * @param[in] max 最大值
 * @param[in] bits 位数
 * @return 映射后的整数值
 */
int motor_float_to_uint(float value, float min, float max, int bits) {
    const float span = max - min;
    if (span <= 0.0f || bits <= 0 || bits >= 31) {
        return 0;
    }

    value = clamp_float(value, min, max);
    const float max_int = (float) ((1u << bits) - 1u);
    return (int) ((value - min) * max_int / span);
}

/**
 * @brief 将无符号整数还原为浮点数
 * @param[in] value 整数值
 * @param[in] min 最小值
 * @param[in] max 最大值
 * @param[in] bits 位数
 * @return 还原后的浮点数值
 */
float motor_uint_to_float(int value, float min, float max, int bits) {
    const float span = max - min;
    if (span <= 0.0f || bits <= 0 || bits >= 31) {
        return min;
    }
    const float max_int = (float) ((1u << bits) - 1u);
    return ((float) value) * span / max_int + min;
}

/**
 * @brief 构建 PVT 控制报文
 * @param[in] config 电机配置
 * @param[in] kp 位置比例增益
 * @param[in] kd 位置微分增益
 * @param[in] position 目标位置（rad）
 * @param[in] speed 目标速度（rad/s）
 * @param[in] torque 目标转矩（Nm）
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_pvt_control(const MotorConfig* config, float kp, float kd, float position,
                             float speed, float torque, MotorPackMsg* packet) {
    MotorRanges ranges;
    if (!valid_config_packet(config, packet) || !motor_get_model_ranges(config->model, &ranges)) {
        return false;
    }

    packet->len = 8;
    const uint16_t kp_int = (uint16_t) motor_float_to_uint(kp, ranges.kp.min, ranges.kp.max, 12);
    const uint16_t kd_int = (uint16_t) motor_float_to_uint(kd, ranges.kd.min, ranges.kd.max, 9);
    const uint16_t pos_int =
        (uint16_t) motor_float_to_uint(position, ranges.position.min, ranges.position.max, 16);
    const uint16_t spd_int =
        (uint16_t) motor_float_to_uint(speed, ranges.speed.min, ranges.speed.max, 12);
    const uint16_t tor_int =
        (uint16_t) motor_float_to_uint(torque, ranges.torque.min, ranges.torque.max, 12);

    packet->data[0] = (uint8_t) (kp_int >> 7);
    packet->data[1] = (uint8_t) (((kp_int & 0x7F) << 1) | ((kd_int & 0x100) >> 8));
    packet->data[2] = (uint8_t) (kd_int & 0xFF);
    packet->data[3] = (uint8_t) (pos_int >> 8);
    packet->data[4] = (uint8_t) (pos_int & 0xFF);
    packet->data[5] = (uint8_t) (spd_int >> 4);
    packet->data[6] = (uint8_t) (((spd_int & 0x0F) << 4) | (tor_int >> 8));
    packet->data[7] = (uint8_t) (tor_int & 0xFF);
    return true;
}

/**
 * @brief 构建位置控制报文
 * @param[in] config 电机配置
 * @param[in] position 目标位置（rad）
 * @param[in] speed 目标速度（rad/s）
 * @param[in] current 目标电流（A）
 * @param[in] feedback 反馈类型（0-3）
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_pos_control(const MotorConfig* config, float position, float speed, float current,
                             uint8_t feedback, MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet) || !valid_feedback(feedback)) {
        return false;
    }

    packet->len = 8;
    const float speed_rpm = speed * 30.0f / MOTOR_PI_F;
    const float position_deg = position * 180.0f / MOTOR_PI_F;
    const uint16_t spd_int = (uint16_t) motor_float_to_uint(speed_rpm, 0.0f, 3276.7f, 15);
    const uint16_t cur_int = (uint16_t) motor_float_to_uint(current, 0.0f, 409.5f, 12);
    uint8_t pos_bytes[4];
    write_float_be(pos_bytes, position_deg);

    packet->data[0] = (uint8_t) (0x20 | (pos_bytes[0] >> 3));
    packet->data[1] = (uint8_t) ((pos_bytes[0] << 5) | (pos_bytes[1] >> 3));
    packet->data[2] = (uint8_t) ((pos_bytes[1] << 5) | (pos_bytes[2] >> 3));
    packet->data[3] = (uint8_t) ((pos_bytes[2] << 5) | (pos_bytes[3] >> 3));
    packet->data[4] = (uint8_t) ((pos_bytes[3] << 5) | (spd_int >> 10));
    packet->data[5] = (uint8_t) ((spd_int & 0x3FC) >> 2);
    packet->data[6] = (uint8_t) (((spd_int & 0x03) << 6) | (cur_int >> 6));
    packet->data[7] = (uint8_t) (((cur_int & 0x3F) << 2) | feedback);
    return true;
}

/**
 * @brief 构建速度控制报文
 * @param[in] config 电机配置
 * @param[in] speed 目标速度（rad/s）
 * @param[in] current 目标电流（A）
 * @param[in] feedback 反馈类型（0-3）
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_spd_control(const MotorConfig* config, float speed, float current,
                             uint8_t feedback, MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet) || !valid_feedback(feedback)) {
        return false;
    }

    packet->len = 7;
    packet->data[0] = (uint8_t) (0x40 | feedback);
    write_float_be(packet->data + 1, speed * 30.0f / MOTOR_PI_F);
    write_u16_be(packet->data + 5, (uint16_t) (current * 10.0f));
    return true;
}

/**
 * @brief 构建电流控制报文
 * @param[in] config 电机配置
 * @param[in] current 目标电流（A）
 * @param[in] feedback 反馈类型（0-3）
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_cur_control(const MotorConfig* config, float current, uint8_t feedback,
                             MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet) || !valid_feedback(feedback)) {
        return false;
    }

    packet->len = 3;
    packet->data[0] = (uint8_t) (0x60 | feedback);
    write_i16_be(packet->data + 1, (int16_t) (current * 100.0f));
    return true;
}

/**
 * @brief 构建转矩控制报文
 * @param[in] config 电机配置
 * @param[in] torque 目标转矩（Nm）
 * @param[in] feedback 反馈类型（0-3）
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_tor_control(const MotorConfig* config, float torque, uint8_t feedback,
                             MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet) || !valid_feedback(feedback)) {
        return false;
    }

    packet->len = 3;
    packet->data[0] = (uint8_t) (0x60 | (1u << 2) | feedback);
    write_i16_be(packet->data + 1, (int16_t) (torque * 100.0f));
    return true;
}

/**
 * @brief 构建刹车控制报文
 * @param[in] config 电机配置
 * @param[in] mode 刹车模式
 * @param[in] current 目标电流（A）
 * @param[in] feedback 反馈类型（0-3）
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_stop(const MotorConfig* config, MotorStopMode mode, float current,
                      uint8_t feedback, MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet) || !valid_feedback(feedback)) {
        return false;
    }

    packet->len = 3;
    packet->data[0] = (uint8_t) ((0x03u << 5) | ((uint8_t) mode << 2) | feedback);
    write_i16_be(packet->data + 1, (int16_t) (current * 100.0f));
    return true;
}

/**
 * @brief 构建抱闸控制报文
 * @param[in] config 电机配置
 * @param[in] enabled 是否启用抱闸
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_brake(const MotorConfig* config, bool enabled, MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet)) {
        return false;
    }

    packet->len = 3;
    packet->data[0] = (uint8_t) (0x04u << 5);
    packet->data[1] = enabled ? 1u : 0u;
    return true;
}

/**
 * @brief 构建设置电机 ID 报文
 * @param[in] config 电机配置
 * @param[in] new_id 新电机 ID
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_id(const MotorConfig* config, uint16_t new_id, MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet)) {
        return false;
    }

    packet->id = 0x7FFu;
    packet->len = 6;
    write_u16_be(packet->data, config->motorId);
    packet->data[2] = 0x00;
    packet->data[3] = 0x04;
    write_u16_be(packet->data + 4, new_id);
    return true;
}

/**
 * @brief 构建设置位置报文
 * @param[in] config 电机配置
 * @param[in] position 目标位置（rad）
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pos(const MotorConfig* config, float position, MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet)) {
        return false;
    }

    packet->id = 0x7FFu;
    packet->len = 6;
    write_u16_be(packet->data, config->motorId);
    packet->data[2] = 0x00;
    packet->data[3] = 0x03;
    write_i16_be(packet->data + 4, (int16_t) (position * 180.0f / MOTOR_PI_F * 100.0f));
    return true;
}

/**
 * @brief 构建复位零位报文
 * @param[in] config 电机配置
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_reset_zero_pos(const MotorConfig* config, MotorPackMsg* packet) {
    return motor_build_set_pos(config, 0.0f, packet);
}

/**
 * @brief 构建设置加速度报文
 * @param[in] config 电机配置
 * @param[in] acceleration 加速度值
 * @param[in] wait_for_ack 是否等待应答
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_acceleration(const MotorConfig* config, float acceleration, bool wait_for_ack,
                                  MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet)) {
        return false;
    }

    const uint16_t acc = (uint16_t) clamp_float(acceleration * 100.0f, 0.0f, 2000.0f);
    packet->len = 4;
    packet->data[0] = (uint8_t) ((0x06u << 5) | (wait_for_ack ? 1u : 0u));
    packet->data[1] = 0x01;
    write_u16_be(packet->data + 2, acc);
    return true;
}

/**
 * @brief 构建设置转矩常数报文
 * @param[in] config 电机配置
 * @param[in] kt 转矩常数
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_kt(const MotorConfig* config, float kt, MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet)) {
        return false;
    }

    packet->len = 4;
    packet->data[0] = (uint8_t) (0x06u << 5);
    packet->data[1] = 0x04;
    write_u16_be(packet->data + 2, (uint16_t) (kt * 100.0f));
    return true;
}

/**
 * @brief 构建设置 uint16_t 范围报文
 * @param[in] config 电机配置
 * @param[in] param 参数子类型
 * @param[in] range 范围值
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
static bool build_set_u16_range(const MotorConfig* config, uint8_t param, UInt16Range range,
                                MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet)) {
        return false;
    }

    packet->len = 6;
    packet->data[0] = (uint8_t) (0x06u << 5);
    packet->data[1] = param;
    write_u16_be(packet->data + 2, range.min);
    write_u16_be(packet->data + 4, range.max);
    return true;
}

/**
 * @brief 构建设置带缩放的 int16_t 范围报文
 * @param[in] config 电机配置
 * @param[in] param 参数子类型
 * @param[in] range 范围值
 * @param[in] scale 缩放系数
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
static bool build_set_i16_range_scaled(const MotorConfig* config, uint8_t param, FloatRange range,
                                       float scale, MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet)) {
        return false;
    }

    packet->len = 6;
    packet->data[0] = (uint8_t) (0x06u << 5);
    packet->data[1] = param;
    write_i16_be(packet->data + 2, (int16_t) (range.min * scale));
    write_i16_be(packet->data + 4, (int16_t) (range.max * scale));
    return true;
}

/**
 * @brief 构建设置 PVT Kp 范围报文
 * @param[in] config 电机配置
 * @param[in] range Kp 范围
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pvt_kp_range(const MotorConfig* config, UInt16Range range,
                                  MotorPackMsg* packet) {
    return build_set_u16_range(config, 0x05, range, packet);
}

/**
 * @brief 构建设置 PVT Kd 范围报文
 * @param[in] config 电机配置
 * @param[in] range Kd 范围
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pvt_kd_range(const MotorConfig* config, UInt16Range range,
                                  MotorPackMsg* packet) {
    return build_set_u16_range(config, 0x06, range, packet);
}

/**
 * @brief 构建设置 PVT 位置范围报文
 * @param[in] config 电机配置
 * @param[in] range 位置范围
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pvt_pos_range(const MotorConfig* config, FloatRange range,
                                   MotorPackMsg* packet) {
    return build_set_i16_range_scaled(config, 0x07, range, 100.0f, packet);
}

/**
 * @brief 构建设置 PVT 速度范围报文
 * @param[in] config 电机配置
 * @param[in] range 速度范围
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pvt_spd_range(const MotorConfig* config, FloatRange range,
                                   MotorPackMsg* packet) {
    return build_set_i16_range_scaled(config, 0x08, range, 100.0f, packet);
}

/**
 * @brief 构建设置 PVT 转矩范围报文
 * @param[in] config 电机配置
 * @param[in] range 转矩范围
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pvt_tor_range(const MotorConfig* config, FloatRange range,
                                   MotorPackMsg* packet) {
    return build_set_i16_range_scaled(config, 0x09, range, 10.0f, packet);
}

/**
 * @brief 构建设置 PVT 电流范围报文
 * @param[in] config 电机配置
 * @param[in] range 电流范围
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pvt_cur_range(const MotorConfig* config, FloatRange range,
                                   MotorPackMsg* packet) {
    return build_set_i16_range_scaled(config, 0x0A, range, 10.0f, packet);
}

/**
 * @brief 构建设置电流环 PI 报文
 * @param[in] config 电机配置
 * @param[in] kp 比例增益
 * @param[in] ki 积分增益
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_cur_pi(const MotorConfig* config, float kp, float ki, MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet)) {
        return false;
    }

    packet->len = 6;
    packet->data[0] = (uint8_t) (0x06u << 5);
    packet->data[1] = 0x0C;
    write_u16_be(packet->data + 2, (uint16_t) (kp * 10000.0f));
    write_u16_be(packet->data + 4, (uint16_t) (ki * 10.0f));
    return true;
}

/**
 * @brief 构建设置速度环 PI 报文
 * @param[in] config 电机配置
 * @param[in] kp 比例增益
 * @param[in] ki 积分增益
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_spd_pi(const MotorConfig* config, float kp, float ki, MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet)) {
        return false;
    }

    packet->len = 6;
    packet->data[0] = (uint8_t) (0x06u << 5);
    packet->data[1] = 0x0D;
    write_u16_be(packet->data + 2, clamp_u16_float(kp * 100000.0f));
    write_u16_be(packet->data + 4, clamp_u16_float(ki * 100000.0f));
    return true;
}

/**
 * @brief 构建设置位置环 PD 报文
 * @param[in] config 电机配置
 * @param[in] kp 比例增益
 * @param[in] kd 微分增益
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pos_pd(const MotorConfig* config, float kp, float kd, MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet)) {
        return false;
    }

    packet->len = 6;
    packet->data[0] = (uint8_t) (0x06u << 5);
    packet->data[1] = 0x0E;
    write_u16_be(packet->data + 2, clamp_u16_float(kp * 100000.0f));
    write_u16_be(packet->data + 4, clamp_u16_float(kd * 100000.0f));
    return true;
}

/**
 * @brief 构建设置 CAN 超时时间报文
 * @param[in] config 电机配置
 * @param[in] timeout_ms 超时时间（毫秒）
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_can_timeout(const MotorConfig* config, uint16_t timeout_ms,
                                 MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet)) {
        return false;
    }

    packet->len = 4;
    packet->data[0] = (uint8_t) (0x06u << 5);
    packet->data[1] = 0x0B;
    write_u16_be(packet->data + 2, timeout_ms);
    return true;
}

/**
 * @brief 构建设置通信模式报文
 * @param[in] config 电机配置
 * @param[in] mode 通信模式
 * @param[in] wait_for_ack 是否等待应答
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_communication_mode(const MotorConfig* config, MotorCommunicationMode mode,
                                        bool wait_for_ack, MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet) || mode > MOTOR_COMM_CAN_OPEN) {
        return false;
    }

    packet->len = 3;
    packet->data[0] = (uint8_t) ((0x06u << 5) | (wait_for_ack ? 1u : 0u));
    packet->data[1] = 0x02;
    packet->data[2] = (uint8_t) mode;
    return true;
}

/**
 * @brief 构建读取参数报文
 * @param[in] config 电机配置
 * @param[in] parameter 要读取的参数
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_get_parameter(const MotorConfig* config, MotorParameter parameter,
                               MotorPackMsg* packet) {
    if (!valid_config_packet(config, packet)) {
        return false;
    }

    packet->len = 2;
    packet->data[0] = (uint8_t) (0x07u << 5);
    packet->data[1] = (uint8_t) parameter;
    return true;
}

/**
 * @brief 打印 feedback1 类型报文
 * @param[in] packet 接收到的报文
 * @param[in] current_range 电流范围（用于解码）
 */
static void print_feedback1(const MotorPackMsg* packet, float current_range) {
    const int pos_int = ((int) packet->data[1] << 8) | packet->data[2];
    const int spd_int = ((int) packet->data[3] << 4) | ((packet->data[4] & 0xF0) >> 4);
    const int cur_int = ((int) (packet->data[4] & 0x0F) << 8) | packet->data[5];
    printf(
        "feedback1 id=%u err=%u pos=%.4f rad spd=%.4f rad/s cur=%.4f A motorTemp=%.1f C "
        "mosTemp=%.1f C\n",
        packet->id, packet->data[0] & 0x1F, motor_uint_to_float(pos_int, -12.5f, 12.5f, 16),
        motor_uint_to_float(spd_int, -18.0f, 18.0f, 12),
        motor_uint_to_float(cur_int, -current_range, current_range, 12),
        ((float) packet->data[6] - 50.0f) / 2.0f, ((float) packet->data[7] - 50.0f) / 2.0f);
}

/**
 * @brief 打印 feedback2 类型报文
 * @param[in] packet 接收到的报文
 */
static void print_feedback2(const MotorPackMsg* packet) {
    const float position = read_float_be(packet->data + 1) / 180.0f * MOTOR_PI_F;
    const int16_t current = read_i16_be(packet->data + 5);
    printf("feedback2 id=%u err=%u pos=%.4f rad cur=%.4f A motorTemp=%.1f C\n", packet->id,
           packet->data[0] & 0x1F, position, (float) current / 100.0f,
           ((float) packet->data[7] - 50.0f) / 2.0f);
}

/**
 * @brief 打印 feedback3 类型报文
 * @param[in] packet 接收到的报文
 */
static void print_feedback3(const MotorPackMsg* packet) {
    const float speed = read_float_be(packet->data + 1) / 30.0f * MOTOR_PI_F;
    const int16_t current = read_i16_be(packet->data + 5);
    printf("feedback3 id=%u err=%u spd=%.4f rad/s cur=%.4f A motorTemp=%.1f C\n", packet->id,
           packet->data[0] & 0x1F, speed, (float) current / 100.0f,
           ((float) packet->data[7] - 50.0f) / 2.0f);
}

/**
 * @brief 打印接收到的电机报文
 * @param[in] packet 接收到的报文
 * @param[in] current_range 电流范围（用于解码反馈报文）
 */
void motor_print_received_packet(const MotorPackMsg* packet, float current_range) {
    if (packet == NULL || packet->len == 0) {
        return;
    }

    const uint8_t group = (uint8_t) (packet->data[0] >> 5);
    if (packet->len >= 8 && group == 1) {
        print_feedback1(packet, current_range);
    } else if (packet->len >= 8 && group == 2) {
        print_feedback2(packet);
    } else if (packet->len >= 8 && group == 3) {
        print_feedback3(packet);
    } else if (packet->len >= 2 && packet->data[0] == 0xFF && packet->data[1] == 0xFE) {
        printf("write-ack id=%u param=%u len=%u\n", packet->id, packet->data[2], packet->len);
    } else if (packet->len >= 2 && group == 5) {
        printf("parameter id=%u param=%u len=%u", packet->id, packet->data[1], packet->len);
        for (uint8_t i = 2; i < packet->len && i < sizeof(packet->data); ++i) {
            printf(" %02x", packet->data[i]);
        }
        printf("\n");
    } else {
        printf("raw id=%u flags=0x%02x len=%u", packet->id, packet->frame_flags, packet->len);
        for (uint8_t i = 0; i < packet->len && i < sizeof(packet->data); ++i) {
            printf(" %02x", packet->data[i]);
        }
        printf("\n");
    }
}
