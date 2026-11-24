// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <ecrt.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "example_igh/external_device.h"
#include "example_igh/motor_layer.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 单路 CAN 时序；从站和总线编号从 0 开始，速率单位 bit/s */
typedef struct {
    uint16_t slave_id;    /**< 从站编号 */
    uint16_t bus_id;      /**< CAN 通道编号 */
    uint32_t bitrate;     /**< 仲裁段波特率 */
    uint32_t dbitrate;    /**< 数据段波特率 */
    double sample_point;  /**< 仲裁段采样点，范围 (0, 1) */
    double dsample_point; /**< 数据段采样点，范围 (0, 1) */
} EcAdapterConfig;

/** @brief EtherCAT 从站 PDO 格式 */
typedef enum {
    EC_SLAVE_FORMAT_NONE = 0,              /*!< 未知/不支持 */
    EC_SLAVE_FORMAT_CLASSIC_CAN_2_BUS,     /*!< Classic CAN，2 路总线 */
    EC_SLAVE_FORMAT_CAN_FD_3_BUS,          /*!< CAN FD，3 路总线 */
    EC_SLAVE_FORMAT_CAN_FD_8_BUS,          /*!< CAN FD，8 路总线 */
    EC_SLAVE_FORMAT_CAN_FD_8_BUS_10_SLOTS, /*!< CAN FD，8 路总线，每路 10 槽位 */
    EC_SLAVE_FORMAT_CAN_FD_3_BUS_10_SLOTS, /*!< CAN FD，3 路总线，每路 10 槽位 */
} EcSlaveFormat;

/** @brief 单个从站的 PDO 布局信息 */
typedef struct {
    EcSlaveFormat format; /*!< 格式类型 */
    size_t bus_count;     /*!< 总线数量 */
    size_t slots_per_bus; /*!< 每路总线槽位数 */
    size_t pdo_size;      /*!< PDO 字节数 */
    size_t motor_offset;  /*!< 电机数据偏移量 */
} EcSlavePdoLayout;

/** @brief 整个主站的布局表 */
typedef struct {
    EcSlavePdoLayout slaves[16]; /*!< 各从站布局 */
    size_t slave_count;          /*!< 从站数量 */
} EcLayoutTable;

/** @brief 单个电机在 PDO 中的偏移信息 */
typedef struct {
    unsigned int id;          /*!< CAN ID 偏移 */
    unsigned int frame_flags; /*!< 帧标志偏移 */
    unsigned int dlc;         /*!< 数据长度偏移 */
    unsigned int data[8];     /*!< 数据字节偏移 */
} EcMotorOffsets;

/** @brief 单个从站的输入/输出偏移信息 */
typedef struct {
    unsigned int out_motor_num;   /*!< 输出电机数量偏移 */
    unsigned int out_can_ide;     /*!< 输出 CAN IDE 偏移 */
    EcMotorOffsets out_motor[80]; /*!< 输出电机偏移数组 */
    unsigned int in_motor_num;    /*!< 输入电机数量偏移 */
    unsigned int in_can_ide;      /*!< 输入 CAN IDE 偏移 */
    EcMotorOffsets in_motor[80];  /*!< 输入电机偏移数组 */
    uint8_t out_sync;             /*!< 420 字节输出同步管理器编号 */
    uint8_t in_sync;              /*!< 420 字节输入同步管理器编号 */
} EcSlaveOffsets;

/** @brief 完整的 CAN 报文，FRAME 仅含 EXT、FD、BRS 和 RTR 位 */
typedef struct {
    uint32_t id;         /**< CAN ID */
    uint8_t frame_flags; /**< 低四位 CAN 标志 */
    uint8_t len;         /**< 整帧有效载荷字节数 */
    uint8_t data[64];    /**< 整帧数据，RTR 数据清零 */
} EcCanFrame;

/** @brief 每周期在收到完整 CAN 报文时调用，报文指针仅在本次调用中有效 */
typedef void (*EcCanFrameHandler)(uint16_t slave_id, uint16_t bus_id, const EcCanFrame* frame,
                                  void* user_data);

enum {
    EC_CAN_FD_GROUPS_PER_BUS = 5,
    EC_CAN_TX_QUEUE_CAPACITY = 64,
    EC_DEFAULT_CYCLE_PERIOD_US = 1000,
};

/** @brief 主站内部 CAN FD 分片重组状态 */
typedef struct {
    EcCanFrame frame;
    uint64_t deadline_ns;
    unsigned int received;
    bool from_previous_pdo;
} EcCanFdGroup;

/** @brief 主站内部 CAN 发送队列项 */
typedef struct {
    EcCanFrame frame;
    uint16_t slave_id;
    uint16_t bus_id;
} EcCanTxEntry;

/** @brief IgH EtherCAT 主站句柄 */
typedef struct {
    ec_master_t* master;                         /*!< IgH 主站指针 */
    ec_slave_config_t* slave_configs[16];        /**< 已配置的受支持从站 */
    ec_domain_t* domain;                         /*!< IgH 域指针 */
    uint8_t* domain_pd;                          /*!< 域过程数据指针 */
    ec_master_state_t master_state;              /*!< 主站状态 */
    ec_domain_state_t domain_state;              /*!< 域状态 */
    EcLayoutTable layout;                        /*!< 布局表 */
    ExternalDeviceState external_devices[16][8]; /*!< 各从站总线的外设状态 */
    EcSlaveOffsets offsets[16];                  /*!< 各从站偏移 */
    uint32_t vendor_ids[16];                     /*!< 厂商 ID */
    uint32_t product_codes[16];                  /*!< 产品代码 */
    unsigned int master_index;                   /*!< 主站索引 */
    unsigned int last_working_counter;           /*!< 上次工作计数器 */
    bool operational;                 /**< 全部已配置从站已进入 OP 且 WKC 有效 */
    unsigned int wkc_error_count;     /**< 当前 100 周期窗口内的坏 WKC 次数 */
    unsigned int wkc_error_iteration; /**< 当前错误统计窗口的周期数 */
    bool initialized;                 /*!< 是否已初始化 */
    EcCanFdGroup can_rx[16][8][EC_CAN_FD_GROUPS_PER_BUS]; /**< 各通道的接收重组区 */
    EcCanTxEntry can_tx[EC_CAN_TX_QUEUE_CAPACITY]; /**< 尚未放入输出 PDO 的 CAN 报文 */
    size_t can_tx_count;                           /**< 待发送报文数量 */
} EcMaster;

/**
 * @brief 根据输出 PDO 字节数识别既有从站布局
 * @param[in] obytes 输出 PDO 字节数
 * @param[out] layout 布局信息输出
 * @return 识别成功返回 true，不支持返回 false
 */
bool ec_identify_pdo_layout(size_t obytes, EcSlavePdoLayout* layout);

/**
 * @brief 根据双向 PDO 字节数识别从站布局，420 字节布局要求双向尺寸一致
 * @param[in] obytes 输出 PDO 字节数
 * @param[in] ibytes 输入 PDO 字节数
 * @param[out] layout 布局信息输出
 * @return 识别成功返回 true
 */
bool ec_identify_pdo_layout_sizes(size_t obytes, size_t ibytes, EcSlavePdoLayout* layout);

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
 * @brief 打开 IgH EtherCAT 主站
 * @param[out] master 主站句柄
 * @param[in] master_index 主站索引字符串（如 "0"）
 * @return 成功返回 true，失败返回 false
 */
bool ec_master_open(EcMaster* master, const char* master_index);

/**
 * @brief 打开主站并在开始 PDO 通信前配置 CAN 时序
 * @param master 主站句柄
 * @param interface_name SOEM 网卡名或 IgH 主站编号
 * @param configs 配置数组；数量为零时可为 NULL，保持固件原配置
 * @param count 配置项数量
 * @return 初始化及配置成功返回 true
 */
bool ec_master_open_with_configs(EcMaster* master, const char* interface_name,
                                 const EcAdapterConfig* configs, size_t count);

/**
 * @brief 初始化期间配置 CAN 时序；SOEM 立即写入，IgH 在激活前注册状态切换 SDO
 * @param master 已发现从站布局、尚未完成初始化的主站
 * @param configs 配置数组；同一从站同一通道不能重复
 * @param count 配置项数量
 * @return 校验和写入或注册成功返回 true；失败时应关闭主站，不开始 PDO 通信
 * @note 仅供初始化阶段调用，不支持运行时修改；下发失败不会回滚已写入项
 */
bool ec_master_configure_adapters(EcMaster* master, const EcAdapterConfig* configs, size_t count);

/**
 * @brief 关闭 IgH EtherCAT 主站
 * @param[in,out] master 主站句柄
 */
void ec_master_close(EcMaster* master);

/**
 * @brief 发送电机报文到指定槽位
 * @param[in,out] master 主站句柄
 * @param[in] config 电机配置
 * @param[in] slot 槽位编号
 * @param[in] packet 要发送的报文
 * @return 已就绪且写入成功返回 true，未就绪或参数无效返回 false
 */
bool ec_master_send_packet(EcMaster* master, const MotorConfig* config, uint16_t slot,
                           const MotorPackMsg* packet);

/**
 * @brief 按总线 FIFO 提交完整 CAN 报文，每帧在同一 PDO 周期发送全部分片
 * @param[in,out] master 主站句柄
 * @param[in] slave_id 从站编号
 * @param[in] bus_id 总线编号
 * @param[in] frame 长度为 1～64 字节的报文，超过 8 字节需要 FD 标志
 * @return 就绪且成功入队返回 true，参数无效、非 CAN FD PDO 布局或队列已满返回 false
 */
bool ec_master_send_can_frame(EcMaster* master, uint16_t slave_id, uint16_t bus_id,
                              const EcCanFrame* frame);

/**
 * @brief 执行一次 EtherCAT 周期循环（接收、处理、发送）
 * @param[in,out] master 主站句柄
 * @return 全部已配置从站为 OP 且本周期 WKC 有效返回 true，恢复期间返回 false
 * @note 断联后应继续调用本函数以驱动自动恢复；恢复期间丢弃新命令并交换空 PDO
 */
bool ec_master_cycle(EcMaster* master);

/**
 * @brief 按指定周期执行 EtherCAT 交换，并回调 CAN FD 布局上收到的完整 CAN 报文
 * @param[in,out] master 主站句柄
 * @param[in] cycle_period_us 调用者配置的循环周期，单位微秒，必须大于 0
 * @param[in] handler 原始 CAN 报文回调，可为 NULL
 * @param[in,out] user_data 传给回调的应用数据
 * @return 全部从站为 OP 且本周期 WKC 有效返回 true
 */
bool ec_master_cycle_with_can_handler(EcMaster* master, uint32_t cycle_period_us,
                                      EcCanFrameHandler handler, void* user_data);

/**
 * @brief 打印主站布局信息
 * @param[in] master 主站句柄
 */
void ec_print_layout(const EcMaster* master);

#ifdef __cplusplus
}
#endif
