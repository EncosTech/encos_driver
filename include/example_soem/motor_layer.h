#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 电机型号枚举 */
typedef enum {
    MOTOR_MODEL_EC_A2806_P2,
    MOTOR_MODEL_EC_A2806_P2_72V,
    MOTOR_MODEL_EC_A4310_P2,
    MOTOR_MODEL_EC_A4310_P2_72V,
    MOTOR_MODEL_EC_A4315_P2,
    MOTOR_MODEL_EC_A4315_P2_72V,
    MOTOR_MODEL_EC_A6408_P2,
    MOTOR_MODEL_EC_A6408_P2_72V,
    MOTOR_MODEL_EC_A6416_P2,
    MOTOR_MODEL_EC_A6416_P2_72V,
    MOTOR_MODEL_EC_A8112_P1,
    MOTOR_MODEL_EC_A8112_P1_72V,
    MOTOR_MODEL_EC_A8116_P1,
    MOTOR_MODEL_EC_A8116_P1_72V,
    MOTOR_MODEL_EC_A10020_P1_12,
    MOTOR_MODEL_EC_A10020_P1_12_72V,
    MOTOR_MODEL_EC_A10020_P1_6,
    MOTOR_MODEL_EC_A10020_P1_6_72V,
    MOTOR_MODEL_EC_A13720_P1,
    MOTOR_MODEL_EC_A13720_P1_72V,
    MOTOR_MODEL_EC_A13715_P1,
    MOTOR_MODEL_EC_A13715_P1_72V,
    MOTOR_MODEL_EC_A4310_P2_H,
    MOTOR_MODEL_EC_A4310_P2_H_72V,
    MOTOR_MODEL_EC_A6408_P2_16H,
    MOTOR_MODEL_EC_A6408_P2_16H_72V,
    MOTOR_MODEL_EC_A6408_P2_32H,
    MOTOR_MODEL_EC_A6408_P2_32H_72V,
    MOTOR_MODEL_EC_A6408_P2_32HB,
    MOTOR_MODEL_EC_A6408_P2_32HB_72V,
    MOTOR_MODEL_EC_A6416_P2_H,
    MOTOR_MODEL_EC_A6416_P2_H_72V,
    MOTOR_MODEL_EC_A8112_P1_H,
    MOTOR_MODEL_EC_A8112_P1_H_72V,
    MOTOR_MODEL_EC_A8116_P1_H,
    MOTOR_MODEL_EC_A8116_P1_H_72V,
    MOTOR_MODEL_EC_A10010_P2,
    MOTOR_MODEL_EC_A10010_P2_72V,
    MOTOR_MODEL_EC_A10020_P2,
    MOTOR_MODEL_EC_A10020_P2_72V,
    MOTOR_MODEL_EC_A3814_H14,
    MOTOR_MODEL_EC_A3814_H14_72V,
    MOTOR_MODEL_EC_A5013_H17,
    MOTOR_MODEL_EC_A5013_H17_72V,
    MOTOR_MODEL_EC_A6013_H20,
    MOTOR_MODEL_EC_A6013_H20_72V,
    MOTOR_MODEL_EC_A10320_P2,
    MOTOR_MODEL_EC_A10320_P2_72V,
    MOTOR_MODEL_EC_A7520_P2,
    MOTOR_MODEL_EC_A7520_P2_72V,
    MOTOR_MODEL_EC_A5020_P2,
    MOTOR_MODEL_EC_A5025_P2,
    MOTOR_MODEL_EC_A7216_P2,
    MOTOR_MODEL_EC_A7220_P2,
    MOTOR_MODEL_EC_A7225_P2,
    MOTOR_MODEL_EC_A9016_P2,
    MOTOR_MODEL_EC_A9020_P2,
    MOTOR_MODEL_EC_A9025_P2,
    MOTOR_MODEL_EC_A10820_P2,
    MOTOR_MODEL_EC_A10825_P2,
    MOTOR_MODEL_COUNT,
} MotorModel;

/** @brief 电机刹车模式 */
typedef enum {
    MOTOR_STOP_FULL_BRAKE = 2,       /**< 变阻尼刹车 */
    MOTOR_STOP_DYNAMIC_BRAKE = 3,    /**< 能耗制动刹车 */
    MOTOR_STOP_REGENERATIVE_BRAKE = 4, /**< 再生制动刹车 */
} MotorStopMode;

/** @brief 电机通信模式 */
typedef enum {
    MOTOR_COMM_CLASSIC_CAN = 0, /**< Classic CAN */
    MOTOR_COMM_CAN_FD = 1,      /**< CAN FD */
    MOTOR_COMM_CAN_OPEN = 2,    /**< CANopen */
} MotorCommunicationMode;

/** @brief 电机参数枚举，用于 get-parameter 指令 */
typedef enum {
    MOTOR_PARAM_POSITION = 1,         /**< 位置 */
    MOTOR_PARAM_SPEED = 2,            /**< 速度 */
    MOTOR_PARAM_CURRENT = 3,          /**< 电流 */
    MOTOR_PARAM_POWER = 4,            /**< 功率 */
    MOTOR_PARAM_ACCELERATION = 5,     /**< 加速度 */
    MOTOR_PARAM_FLUX_KP_GAIN = 6,     /**< 磁通 Kp */
    MOTOR_PARAM_FLUX_KI_GAIN = 7,     /**< 磁通 Ki */
    MOTOR_PARAM_FB_KP_GAIN = 8,       /**< 反馈 Kp */
    MOTOR_PARAM_PD_GAIN = 9,          /**< PD 增益 */
    MOTOR_PARAM_KT = 22,              /**< 转矩常数 */
    MOTOR_PARAM_PVT_KP_RANGE = 23,    /**< PVT Kp 范围 */
    MOTOR_PARAM_PVT_KD_RANGE = 24,    /**< PVT Kd 范围 */
    MOTOR_PARAM_PVT_POS_RANGE = 25,   /**< PVT 位置范围 */
    MOTOR_PARAM_PVT_SPD_RANGE = 26,   /**< PVT 速度范围 */
    MOTOR_PARAM_PVT_TOR_RANGE = 27,   /**< PVT 转矩范围 */
    MOTOR_PARAM_PVT_CUR_RANGE = 28,   /**< PVT 电流范围 */
    MOTOR_PARAM_UUID = 29,            /**< UUID */
    MOTOR_PARAM_VERSION = 30,         /**< 固件版本 */
    MOTOR_PARAM_CAN_TIMEOUT = 31,     /**< CAN 超时时间 */
    MOTOR_PARAM_CUR_KP_KI = 32,       /**< 电流环 PI */
    MOTOR_PARAM_SPD_KP_KI = 33,       /**< 速度环 PI */
    MOTOR_PARAM_POS_KP_KD = 34,       /**< 位置环 PD */
    MOTOR_PARAM_BRAKE_STATUS = 37,    /**< 抱闸状态 */
} MotorParameter;

/** @brief 浮点数范围 */
typedef struct {
    float min; /**< 最小值 */
    float max; /**< 最大值 */
} FloatRange;

/** @brief 16位无符号整数范围 */
typedef struct {
    uint16_t min; /**< 最小值 */
    uint16_t max; /**< 最大值 */
} UInt16Range;

/** @brief 电机各控制量的范围 */
typedef struct {
    FloatRange kp;       /**< 位置比例增益范围 */
    FloatRange kd;       /**< 位置微分增益范围 */
    FloatRange position; /**< 位置范围 */
    FloatRange speed;    /**< 速度范围 */
    FloatRange torque;   /**< 转矩范围 */
    FloatRange current;  /**< 电流范围 */
    float kt;            /**< 转矩常数 */
} MotorRanges;

/** @brief 电机配置 */
typedef struct {
    uint16_t slaveId;  /**< 从站编号 */
    uint16_t busId;    /**< 总线编号 */
    uint16_t motorId;  /**< 电机编号 */
    uint8_t flag;      /**< CAN 帧标志 */
    MotorModel model;  /**< 电机型号 */
} MotorConfig;

#pragma pack(push, 1)
/** @brief 打包后的电机 CAN 报文 */
typedef struct {
    uint32_t id;         /**< CAN ID */
    uint8_t frame_flags; /**< 帧标志 */
    uint8_t len;         /**< 数据长度 */
    uint8_t data[8];     /**< 数据 */
} MotorPackMsg;
#pragma pack(pop)

/** @brief CAN 帧标志位 */
enum {
    MOTOR_CAN_FLAG_EFF = 0x01,                         /**< 扩展帧标志 */
    MOTOR_CAN_FLAG_FD_BIT1 = 0x02,                     /**< CAN FD 标志位1 */
    MOTOR_CAN_FLAG_FD_BIT2 = 0x04,                     /**< CAN FD 标志位2 */
    MOTOR_CAN_FLAG_RTR = 0x08,                         /**< 远程帧标志 */
    MOTOR_CAN_FLAG_MASK = 0x0F,                        /**< 标志位掩码 */
    MOTOR_CAN_FLAG_FD_MASK = MOTOR_CAN_FLAG_FD_BIT1 | MOTOR_CAN_FLAG_FD_BIT2, /**< CAN FD 掩码 */
};

/**
 * @brief 创建电机配置（默认标准帧）
 * @param[in] slave_id 从站编号
 * @param[in] bus_id 总线编号
 * @param[in] motor_id 电机编号
 * @param[in] model 电机型号
 * @return 电机配置结构体
 */
MotorConfig motor_config_make(uint16_t slave_id, uint16_t bus_id, uint16_t motor_id,
                              MotorModel model);

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
                                         MotorModel model, bool eff, bool canfd);

/**
 * @brief 获取指定型号电机的控制量范围
 * @param[in] model 电机型号
 * @param[out] ranges 控制量范围输出
 * @return 成功返回 true，失败返回 false
 */
bool motor_get_model_ranges(MotorModel model, MotorRanges* ranges);

/**
 * @brief 规范化 CAN 帧标志
 * @param[in] flags 原始标志
 * @return 规范化后的标志
 */
uint8_t motor_sanitize_flags(uint8_t flags);

/**
 * @brief 将浮点数映射为无符号整数
 * @param[in] value 浮点数值
 * @param[in] min 最小值
 * @param[in] max 最大值
 * @param[in] bits 位数
 * @return 映射后的整数值
 */
int motor_float_to_uint(float value, float min, float max, int bits);

/**
 * @brief 将无符号整数还原为浮点数
 * @param[in] value 整数值
 * @param[in] min 最小值
 * @param[in] max 最大值
 * @param[in] bits 位数
 * @return 还原后的浮点数值
 */
float motor_uint_to_float(int value, float min, float max, int bits);

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
                             float speed, float torque, MotorPackMsg* packet);

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
                             uint8_t feedback, MotorPackMsg* packet);

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
                             uint8_t feedback, MotorPackMsg* packet);

/**
 * @brief 构建电流控制报文
 * @param[in] config 电机配置
 * @param[in] current 目标电流（A）
 * @param[in] feedback 反馈类型（0-3）
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_cur_control(const MotorConfig* config, float current, uint8_t feedback,
                             MotorPackMsg* packet);

/**
 * @brief 构建转矩控制报文
 * @param[in] config 电机配置
 * @param[in] torque 目标转矩（Nm）
 * @param[in] feedback 反馈类型（0-3）
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_tor_control(const MotorConfig* config, float torque, uint8_t feedback,
                             MotorPackMsg* packet);

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
                      uint8_t feedback, MotorPackMsg* packet);

/**
 * @brief 构建抱闸控制报文
 * @param[in] config 电机配置
 * @param[in] enabled 是否启用抱闸
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_brake(const MotorConfig* config, bool enabled, MotorPackMsg* packet);

/**
 * @brief 构建设置电机 ID 报文
 * @param[in] config 电机配置
 * @param[in] new_id 新电机 ID
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_id(const MotorConfig* config, uint16_t new_id, MotorPackMsg* packet);

/**
 * @brief 构建设置位置报文
 * @param[in] config 电机配置
 * @param[in] position 目标位置（rad）
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pos(const MotorConfig* config, float position, MotorPackMsg* packet);

/**
 * @brief 构建复位零位报文
 * @param[in] config 电机配置
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_reset_zero_pos(const MotorConfig* config, MotorPackMsg* packet);

/**
 * @brief 构建设置加速度报文
 * @param[in] config 电机配置
 * @param[in] acceleration 加速度值
 * @param[in] wait_for_ack 是否等待应答
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_acceleration(const MotorConfig* config, float acceleration, bool wait_for_ack,
                                  MotorPackMsg* packet);

/**
 * @brief 构建设置转矩常数报文
 * @param[in] config 电机配置
 * @param[in] kt 转矩常数
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_kt(const MotorConfig* config, float kt, MotorPackMsg* packet);

/**
 * @brief 构建设置 PVT Kp 范围报文
 * @param[in] config 电机配置
 * @param[in] range Kp 范围
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pvt_kp_range(const MotorConfig* config, UInt16Range range,
                                  MotorPackMsg* packet);

/**
 * @brief 构建设置 PVT Kd 范围报文
 * @param[in] config 电机配置
 * @param[in] range Kd 范围
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pvt_kd_range(const MotorConfig* config, UInt16Range range,
                                  MotorPackMsg* packet);

/**
 * @brief 构建设置 PVT 位置范围报文
 * @param[in] config 电机配置
 * @param[in] range 位置范围
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pvt_pos_range(const MotorConfig* config, FloatRange range,
                                   MotorPackMsg* packet);

/**
 * @brief 构建设置 PVT 速度范围报文
 * @param[in] config 电机配置
 * @param[in] range 速度范围
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pvt_spd_range(const MotorConfig* config, FloatRange range,
                                   MotorPackMsg* packet);

/**
 * @brief 构建设置 PVT 转矩范围报文
 * @param[in] config 电机配置
 * @param[in] range 转矩范围
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pvt_tor_range(const MotorConfig* config, FloatRange range,
                                   MotorPackMsg* packet);

/**
 * @brief 构建设置 PVT 电流范围报文
 * @param[in] config 电机配置
 * @param[in] range 电流范围
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pvt_cur_range(const MotorConfig* config, FloatRange range,
                                   MotorPackMsg* packet);

/**
 * @brief 构建设置电流环 PI 报文
 * @param[in] config 电机配置
 * @param[in] kp 比例增益
 * @param[in] ki 积分增益
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_cur_pi(const MotorConfig* config, float kp, float ki, MotorPackMsg* packet);

/**
 * @brief 构建设置速度环 PI 报文
 * @param[in] config 电机配置
 * @param[in] kp 比例增益
 * @param[in] ki 积分增益
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_spd_pi(const MotorConfig* config, float kp, float ki, MotorPackMsg* packet);

/**
 * @brief 构建设置位置环 PD 报文
 * @param[in] config 电机配置
 * @param[in] kp 比例增益
 * @param[in] kd 微分增益
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_pos_pd(const MotorConfig* config, float kp, float kd, MotorPackMsg* packet);

/**
 * @brief 构建设置 CAN 超时时间报文
 * @param[in] config 电机配置
 * @param[in] timeout_ms 超时时间（毫秒）
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_can_timeout(const MotorConfig* config, uint16_t timeout_ms,
                                 MotorPackMsg* packet);

/**
 * @brief 构建设置通信模式报文
 * @param[in] config 电机配置
 * @param[in] mode 通信模式
 * @param[in] wait_for_ack 是否等待应答
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_set_communication_mode(const MotorConfig* config, MotorCommunicationMode mode,
                                        bool wait_for_ack, MotorPackMsg* packet);

/**
 * @brief 构建读取参数报文
 * @param[in] config 电机配置
 * @param[in] parameter 要读取的参数
 * @param[out] packet 输出报文
 * @return 成功返回 true，失败返回 false
 */
bool motor_build_get_parameter(const MotorConfig* config, MotorParameter parameter,
                               MotorPackMsg* packet);

/**
 * @brief 打印接收到的电机报文
 * @param[in] packet 接收到的报文
 * @param[in] current_range 电流范围（用于解码反馈报文）
 */
void motor_print_received_packet(const MotorPackMsg* packet, float current_range);

#ifdef __cplusplus
}
#endif
