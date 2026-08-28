#pragma once

#include <soem/soem.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "example_soem/external_device.h"
#include "example_soem/motor_layer.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief EtherCAT 从站 PDO 格式 */
typedef enum {
    EC_SLAVE_FORMAT_NONE = 0,          /**< 未知/不支持 */
    EC_SLAVE_FORMAT_CLASSIC_CAN_2_BUS,          /**< Classic CAN，2 路总线 */
    EC_SLAVE_FORMAT_CAN_FD_3_BUS,               /**< CAN FD，3 路总线 */
    EC_SLAVE_FORMAT_CAN_FD_8_BUS,               /**< CAN FD，8 路总线 */
    EC_SLAVE_FORMAT_CAN_FD_8_BUS_10_SLOTS,      /**< CAN FD，8 路总线，每路 10 槽位 */
} EcSlaveFormat;

/** @brief 单个从站的 PDO 布局信息 */
typedef struct {
    EcSlaveFormat format; /**< 格式类型 */
    size_t bus_count;     /**< 总线数量 */
    size_t slots_per_bus; /**< 每路总线槽位数 */
    size_t pdo_size;      /**< PDO 字节数 */
    size_t motor_offset;  /**< 电机数据偏移量 */
} EcSlavePdoLayout;

/** @brief 整个主站的布局表 */
typedef struct {
    EcSlavePdoLayout slaves[16]; /**< 各从站布局 */
    size_t slave_count;          /**< 从站数量 */
} EcLayoutTable;

/** @brief SOEM EtherCAT 主站句柄 */
typedef struct {
    ecx_contextt ctx;                            /**< SOEM 上下文 */
    EcLayoutTable layout;                        /**< 布局表 */
    ExternalDeviceState external_devices[16][8]; /**< 各从站总线的外设状态 */
    uint8_t io_map[4096];                        /**< IO 映射缓冲区 */
    int expected_wkc;                            /**< 期望的工作计数器 */
    int last_wkc;                                /**< 上次工作计数器 */
    bool initialized;                            /**< 是否已初始化 */
} EcMaster;

/**
 * @brief 根据输出 PDO 字节数识别从站布局格式
 * @param[in] obytes 输出 PDO 字节数
 * @param[out] layout 布局信息输出
 * @return 识别成功返回 true，不支持返回 false
 */
bool ec_identify_pdo_layout(size_t obytes, EcSlavePdoLayout* layout);

/**
 * @brief 验证目标从站/总线/槽位是否有效
 * @param[in] table 布局表
 * @param[in] slave_id 从站编号
 * @param[in] bus_id 总线编号
 * @param[in] slot 槽位编号
 * @return 有效返回 true，无效返回 false
 */
bool ec_validate_target(const EcLayoutTable* table, uint16_t slave_id, uint16_t bus_id,
                        uint16_t slot);

/**
 * @brief 计算指定槽位在 PDO 中的字节偏移
 * @param[in] layout 从站布局
 * @param[in] bus_id 总线编号
 * @param[in] slot 槽位编号
 * @return 字节偏移量
 */
size_t ec_motor_slot_offset(const EcSlavePdoLayout* layout, uint16_t bus_id, uint16_t slot);

/**
 * @brief 将电机报文写入从站输出 PDO
 * @param[in,out] slave_output 从站输出缓冲区
 * @param[in] layout 从站布局
 * @param[in] bus_id 总线编号
 * @param[in] slot 槽位编号
 * @param[in] packet 要写入的报文
 * @return 成功返回 true，失败返回 false
 */
bool ec_write_motor_packet(uint8_t* slave_output, const EcSlavePdoLayout* layout, uint16_t bus_id,
                           uint16_t slot, const MotorPackMsg* packet);

/**
 * @brief 从从站输入 PDO 读取电机报文
 * @param[in] slave_input 从站输入缓冲区
 * @param[in] layout 从站布局
 * @param[in] bus_id 总线编号
 * @param[in] slot 槽位编号
 * @param[out] packet 读取到的报文
 * @return 成功读取到有效数据返回 true，否则返回 false
 */
bool ec_read_motor_packet(const uint8_t* slave_input, const EcSlavePdoLayout* layout,
                          uint16_t bus_id, uint16_t slot, MotorPackMsg* packet);

/**
 * @brief 打开 SOEM EtherCAT 主站
 * @param[out] master 主站句柄
 * @param[in] ifname 网卡接口名（如 "eth0"）
 * @return 成功返回 true，失败返回 false
 */
bool ec_master_open(EcMaster* master, const char* ifname);

/**
 * @brief 关闭 SOEM EtherCAT 主站
 * @param[in,out] master 主站句柄
 */
void ec_master_close(EcMaster* master);

/**
 * @brief 发送电机报文到指定槽位
 * @param[in,out] master 主站句柄
 * @param[in] config 电机配置
 * @param[in] slot 槽位编号
 * @param[in] packet 要发送的报文
 * @return 成功返回 true，失败返回 false
 */
bool ec_master_send_packet(EcMaster* master, const MotorConfig* config, uint16_t slot,
                           const MotorPackMsg* packet);

/**
 * @brief 执行一次 EtherCAT 周期循环（发送、接收、读取回包）
 * @param[in,out] master 主站句柄
 * @return 工作计数器完整返回 true，否则返回 false
 */
bool ec_master_cycle(EcMaster* master);

/**
 * @brief 打印主站布局信息
 * @param[in] master 主站句柄
 */
void ec_print_layout(const EcMaster* master);

#ifdef __cplusplus
}
#endif
