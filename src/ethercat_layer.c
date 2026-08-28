#include "example_igh/ethercat_layer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    EC_CLASSIC_CAN_2_BUS_SIZE = 86,
    EC_CLASSIC_CAN_8_BUS_SIZE = 340,
    EC_CLASSIC_CAN_8_BUS_MAPPED_SIZE = 338,
    EC_CAN_FD_3_BUS_SIZE = 336,
    EC_CAN_FD_8_BUS_SIZE = 896,
    EC_CAN_FD_8_BUS_10_SLOTS_SIZE = 1120,
    EC_MAX_LAYOUT_SLAVES = 16,
    EC_MAX_MOTOR_SLOTS = 80,
    EC_CAN_FD_SLOTS_PER_BUS = 10,
    EC_CLASSIC_CAN_2_BUS_SLOTS = 6,
    EC_PDO_FIELDS_PER_MOTOR = 11,
    EC_CLASSIC_HEADER_ENTRIES = 2,
    EC_CLASSIC_MAX_PDO_ENTRIES = EC_CLASSIC_HEADER_ENTRIES + (24 * EC_PDO_FIELDS_PER_MOTOR),
    EC_CAN_FD_PDO_ENTRIES = EC_CAN_FD_SLOTS_PER_BUS * EC_PDO_FIELDS_PER_MOTOR,
    EC_MAX_DOMAIN_REGS = EC_MAX_LAYOUT_SLAVES * EC_MAX_MOTOR_SLOTS * EC_PDO_FIELDS_PER_MOTOR * 2,
};

/**
 * @brief 获取 PDO 格式的名称字符串
 * @param[in] format 格式枚举
 * @return 格式名称
 */
static const char* format_name(EcSlaveFormat format) {
    switch (format) {
        case EC_SLAVE_FORMAT_CLASSIC_CAN_2_BUS:
            return "ClassicCan2Bus";
        case EC_SLAVE_FORMAT_CAN_FD_3_BUS:
            return "CanFd3Bus";
        case EC_SLAVE_FORMAT_CAN_FD_8_BUS:
            return "CanFd8Bus";
        case EC_SLAVE_FORMAT_CAN_FD_8_BUS_10_SLOTS:
            return "CanFd8Bus10Slots";
        case EC_SLAVE_FORMAT_NONE:
        default:
            return "Unsupported";
    }
}

/**
 * @brief 计算布局的总槽位数
 * @param[in] layout 从站布局
 * @return 总槽位数
 */
static size_t layout_slot_count(const EcSlavePdoLayout* layout) {
    if (layout == NULL) {
        return 0;
    }
    return layout->bus_count * layout->slots_per_bus;
}

/**
 * @brief 根据输出 PDO 字节数识别从站布局格式
 * @param[in] obytes 输出 PDO 字节数
 * @param[out] layout 布局信息输出
 * @return 识别成功返回 true，不支持返回 false
 */
bool ec_identify_pdo_layout(size_t obytes, EcSlavePdoLayout* layout) {
    if (layout == NULL) {
        return false;
    }

    memset(layout, 0, sizeof(*layout));
    layout->pdo_size = obytes;

    if (obytes == EC_CLASSIC_CAN_2_BUS_SIZE) {
        layout->format = EC_SLAVE_FORMAT_CLASSIC_CAN_2_BUS;
        layout->bus_count = 2;
        layout->slots_per_bus = 3;
        layout->motor_offset = 2;
        return true;
    }
    if (obytes == EC_CAN_FD_3_BUS_SIZE) {
        layout->format = EC_SLAVE_FORMAT_CAN_FD_3_BUS;
        layout->bus_count = 3;
        layout->slots_per_bus = 8;
        layout->motor_offset = 0;
        return true;
    }
    if (obytes == EC_CAN_FD_8_BUS_SIZE) {
        layout->format = EC_SLAVE_FORMAT_CAN_FD_8_BUS;
        layout->bus_count = 8;
        layout->slots_per_bus = 8;
        layout->motor_offset = 0;
        return true;
    }
    if (obytes == EC_CAN_FD_8_BUS_10_SLOTS_SIZE) {
        layout->format = EC_SLAVE_FORMAT_CAN_FD_8_BUS_10_SLOTS;
        layout->bus_count = 8;
        layout->slots_per_bus = 10;
        layout->motor_offset = 0;
        return true;
    }

    layout->format = EC_SLAVE_FORMAT_NONE;
    return false;
}

/**
 * @brief 验证目标从站/总线/槽位是否有效
 * @param[in] table 布局表
 * @param[in] slave_id 从站编号
 * @param[in] bus_id 总线编号
 * @param[in] slot 槽位编号
 * @return 有效返回 true，无效返回 false
 */
bool ec_validate_target(const EcLayoutTable* table, uint16_t slave_id, uint16_t bus_id,
                        uint16_t slot) {
    if (table == NULL || slave_id >= table->slave_count) {
        return false;
    }

    const EcSlavePdoLayout* layout = &table->slaves[slave_id];
    if (layout->format == EC_SLAVE_FORMAT_NONE) {
        return false;
    }
    if (bus_id >= layout->bus_count || slot >= layout->slots_per_bus) {
        return false;
    }

    return ec_motor_slot_offset(layout, bus_id, slot) + sizeof(MotorPackMsg) <= layout->pdo_size;
}

/**
 * @brief 计算指定槽位在 PDO 中的字节偏移
 * @param[in] layout 从站布局
 * @param[in] bus_id 总线编号
 * @param[in] slot 槽位编号
 * @return 字节偏移量
 */
size_t ec_motor_slot_offset(const EcSlavePdoLayout* layout, uint16_t bus_id, uint16_t slot) {
    if (layout == NULL) {
        return 0;
    }

    const size_t motor_index = ((size_t) bus_id * layout->slots_per_bus) + slot;
    return layout->motor_offset + (motor_index * sizeof(MotorPackMsg));
}

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
                           uint16_t slot, const MotorPackMsg* packet) {
    if (slave_output == NULL || layout == NULL || packet == NULL) {
        return false;
    }
    if (bus_id >= layout->bus_count || slot >= layout->slots_per_bus) {
        return false;
    }

    const size_t offset = ec_motor_slot_offset(layout, bus_id, slot);
    if (offset + sizeof(*packet) > layout->pdo_size) {
        return false;
    }

    if (layout->format == EC_SLAVE_FORMAT_CLASSIC_CAN_2_BUS) {
        uint8_t* motor_num = slave_output;
        uint8_t* can_ide = slave_output + 1;
        if (*motor_num < layout_slot_count(layout)) {
            ++(*motor_num);
        }
        if ((packet->frame_flags & MOTOR_CAN_FLAG_EFF) != 0) {
            *can_ide = 1;
        }
    }

    memcpy(slave_output + offset, packet, sizeof(*packet));
    return true;
}

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
                          uint16_t bus_id, uint16_t slot, MotorPackMsg* packet) {
    if (slave_input == NULL || layout == NULL || packet == NULL) {
        return false;
    }
    if (bus_id >= layout->bus_count || slot >= layout->slots_per_bus) {
        return false;
    }

    const size_t offset = ec_motor_slot_offset(layout, bus_id, slot);
    if (offset + sizeof(*packet) > layout->pdo_size) {
        return false;
    }

    memcpy(packet, slave_input + offset, sizeof(*packet));
    return packet->len != 0;
}

/**
 * @brief 生成 Classic CAN 从站的 PDO Entry 配置
 * @param[in] index PDO 索引
 * @param[in] motor_slots 电机槽位数
 * @param[out] entries PDO Entry 数组
 * @param[out] entry_count Entry 数量
 */
static void make_classic_pdo_entries(uint16_t index, size_t motor_slots,
                                     ec_pdo_entry_info_t entries[EC_CLASSIC_MAX_PDO_ENTRIES],
                                     unsigned int* entry_count) {
    *entry_count =
        (unsigned int) (EC_CLASSIC_HEADER_ENTRIES + (motor_slots * EC_PDO_FIELDS_PER_MOTOR));
    for (unsigned int i = 0; i < *entry_count; ++i) {
        const uint8_t subindex = (uint8_t) (i + 1);
        const uint8_t bit_length =
            (i >= EC_CLASSIC_HEADER_ENTRIES &&
             ((i - EC_CLASSIC_HEADER_ENTRIES) % EC_PDO_FIELDS_PER_MOTOR == 0))
                ? 32
                : 8;
        entries[i] = (ec_pdo_entry_info_t){index, subindex, bit_length};
    }
}

/**
 * @brief 生成 CAN FD 从站的 PDO Entry 配置
 * @param[in] index PDO 索引
 * @param[in] slots_per_bus 每路总线槽位数
 * @param[out] entries PDO Entry 数组
 */
static void make_can_fd_pdo_entries(uint16_t index, size_t slots_per_bus,
                                    ec_pdo_entry_info_t entries[EC_CAN_FD_PDO_ENTRIES]) {
    const unsigned int entry_count = (unsigned int) (slots_per_bus * EC_PDO_FIELDS_PER_MOTOR);
    for (unsigned int i = 0; i < entry_count; ++i) {
        const uint8_t subindex = (uint8_t) (i + 1);
        const uint8_t bit_length = (i % EC_PDO_FIELDS_PER_MOTOR == 0) ? 32 : 8;
        entries[i] = (ec_pdo_entry_info_t){index, subindex, bit_length};
    }
}

/**
 * @brief 检测从站输出 PDO 的字节数
 * @param[in] master IgH 主站指针
 * @param[in] slave_pos 从站位置
 * @param[in] sync_count 同步管理器数量
 * @return 输出 PDO 字节数
 */
static size_t detect_output_bytes(ec_master_t* master, uint16_t slave_pos, uint8_t sync_count) {
    size_t output_bits = 0;
    for (uint8_t sync = 0; sync < sync_count; ++sync) {
        ec_sync_info_t sync_info = {0};
        if (ecrt_master_get_sync_manager(master, slave_pos, sync, &sync_info) != 0) {
            continue;
        }
        if (sync_info.dir != EC_DIR_OUTPUT || sync_info.n_pdos == 0) {
            continue;
        }

        for (uint16_t pdo_pos = 0; pdo_pos < sync_info.n_pdos; ++pdo_pos) {
            ec_pdo_info_t pdo_info = {0};
            if (ecrt_master_get_pdo(master, slave_pos, sync, pdo_pos, &pdo_info) != 0) {
                continue;
            }
            for (uint16_t entry_pos = 0; entry_pos < pdo_info.n_entries; ++entry_pos) {
                ec_pdo_entry_info_t entry = {0};
                if (ecrt_master_get_pdo_entry(master, slave_pos, sync, pdo_pos, entry_pos,
                                              &entry) == 0) {
                    output_bits += entry.bit_length;
                }
            }
        }
    }

    return (output_bits + 7U) / 8U;
}

/**
 * @brief 配置所有从站的 PDO 布局
 * @param[in,out] master 主站句柄
 * @return 成功返回 true，失败返回 false
 */
static bool configure_layouts(EcMaster* master) {
    ec_master_info_t master_info = {0};
    if (ecrt_master(master->master, &master_info) != 0) {
        fprintf(stderr, "Failed to query IGH master info.\n");
        return false;
    }
    if (master_info.slave_count == 0) {
        fprintf(stderr, "No EtherCAT slaves found on IGH master %u.\n", master->master_index);
        return false;
    }
    if (master_info.slave_count > EC_MAX_LAYOUT_SLAVES) {
        fprintf(stderr, "Too many slaves: %u, max supported: %d\n", master_info.slave_count,
                EC_MAX_LAYOUT_SLAVES);
        return false;
    }

    master->layout.slave_count = master_info.slave_count;
    for (size_t slave = 0; slave < master->layout.slave_count; ++slave) {
        ec_slave_info_t slave_info = {0};
        if (ecrt_master_get_slave(master->master, (uint16_t) slave, &slave_info) != 0) {
            fprintf(stderr, "Failed to query IGH slave %zu info.\n", slave);
            return false;
        }

        master->vendor_ids[slave] = slave_info.vendor_id;
        master->product_codes[slave] = slave_info.product_code;

        size_t obytes =
            detect_output_bytes(master->master, (uint16_t) slave, (uint8_t) slave_info.sync_count);
        if (obytes == EC_CLASSIC_CAN_8_BUS_MAPPED_SIZE || obytes == EC_CLASSIC_CAN_8_BUS_SIZE) {
            obytes = EC_CLASSIC_CAN_8_BUS_SIZE;
        }

        EcSlavePdoLayout* layout = &master->layout.slaves[slave];
        if (ec_identify_pdo_layout(obytes, layout)) {
            printf("slaveId=%zu ighSlave=%zu format=%s busCount=%zu slotsPerBus=%zu pdo=%zu\n",
                   slave, slave, format_name(layout->format), layout->bus_count,
                   layout->slots_per_bus, layout->pdo_size);
        } else {
            printf("slaveId=%zu ighSlave=%zu unsupported pdo output bytes=%zu\n", slave, slave,
                   obytes);
        }
    }

    return true;
}

/**
 * @brief 向 PDO 注册表中添加一个电机的寄存器映射
 * @param[in,out] regs PDO 注册表
 * @param[in,out] reg_count 当前注册数量
 * @param[in] slave 从站位置
 * @param[in] vendor_id 厂商 ID
 * @param[in] product_code 产品代码
 * @param[in] index PDO 索引
 * @param[in] base 起始子索引
 * @param[out] offsets 偏移信息输出
 */
static void add_motor_regs(ec_pdo_entry_reg_t* regs, size_t* reg_count, uint16_t slave,
                           uint32_t vendor_id, uint32_t product_code, uint16_t index, uint8_t base,
                           EcMotorOffsets* offsets) {
    regs[(*reg_count)++] =
        (ec_pdo_entry_reg_t){0, slave, vendor_id, product_code, index, base, &offsets->id, NULL};
    regs[(*reg_count)++] = (ec_pdo_entry_reg_t){
        0,   slave, vendor_id, product_code, index, (uint8_t) (base + 1), &offsets->frame_flags,
        NULL};
    regs[(*reg_count)++] = (ec_pdo_entry_reg_t){
        0, slave, vendor_id, product_code, index, (uint8_t) (base + 2), &offsets->dlc, NULL};
    for (size_t i = 0; i < sizeof(offsets->data) / sizeof(offsets->data[0]); ++i) {
        regs[(*reg_count)++] = (ec_pdo_entry_reg_t){
            0,   slave, vendor_id, product_code, index, (uint8_t) (base + 3 + i), &offsets->data[i],
            NULL};
    }
}

/**
 * @brief 配置 Classic CAN 从站的 PDO
 * @param[in,out] master 主站句柄
 * @param[in,out] slave_config 从站配置
 * @param[in,out] regs PDO 注册表
 * @param[in,out] reg_count 注册数量
 * @param[in] slave 从站位置
 * @param[in] motor_slots 电机槽位数
 * @return 成功返回 true，失败返回 false
 */
static bool configure_classic_slave(EcMaster* master, ec_slave_config_t* slave_config,
                                    ec_pdo_entry_reg_t* regs, size_t* reg_count, size_t slave,
                                    size_t motor_slots) {
    ec_pdo_entry_info_t out_entries[EC_CLASSIC_MAX_PDO_ENTRIES];
    ec_pdo_entry_info_t in_entries[EC_CLASSIC_MAX_PDO_ENTRIES];
    unsigned int out_entry_count = 0;
    unsigned int in_entry_count = 0;
    make_classic_pdo_entries(0x7010, motor_slots, out_entries, &out_entry_count);
    make_classic_pdo_entries(0x6000, motor_slots, in_entries, &in_entry_count);

    ec_pdo_entry_info_t input_meta[] = {
        {0x6020, 0x01, 1}, {0x6020, 0x02, 1},  {0x6020, 0x03, 2},
        {0x6020, 0x05, 2}, {0x0000, 0x00, 8},  {0x1802, 0x07, 1},
        {0x1802, 0x09, 1}, {0x6020, 0x0b, 16}, {0x6020, 0x0c, 16},
    };
    ec_pdo_info_t pdos[] = {
        {0x1601, out_entry_count, out_entries},
        {0x1a00, in_entry_count, in_entries},
        {0x1a02, (unsigned int) (sizeof(input_meta) / sizeof(input_meta[0])), input_meta},
    };
    ec_sync_info_t syncs[] = {
        {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
        {1, EC_DIR_INPUT, 0, NULL, EC_WD_DISABLE},
        {2, EC_DIR_OUTPUT, 1, &pdos[0], EC_WD_ENABLE},
        {3, EC_DIR_INPUT, 2, &pdos[1], EC_WD_DISABLE},
        {0xff, 0, 0, NULL, 0},
    };

    if (ecrt_slave_config_pdos(slave_config, EC_END, syncs) != 0) {
        fprintf(stderr, "ecrt_slave_config_pdos failed at slave %zu.\n", slave);
        return false;
    }

    const uint16_t slave_pos = (uint16_t) slave;
    const uint32_t vendor_id = master->vendor_ids[slave];
    const uint32_t product_code = master->product_codes[slave];
    EcSlaveOffsets* off = &master->offsets[slave];
    regs[(*reg_count)++] = (ec_pdo_entry_reg_t){
        0, slave_pos, vendor_id, product_code, 0x7010, 1, &off->out_motor_num, NULL};
    regs[(*reg_count)++] = (ec_pdo_entry_reg_t){0,      slave_pos, vendor_id,         product_code,
                                                0x7010, 2,         &off->out_can_ide, NULL};
    for (size_t motor = 0; motor < motor_slots; ++motor) {
        const uint8_t base = (uint8_t) (3 + (motor * EC_PDO_FIELDS_PER_MOTOR));
        add_motor_regs(regs, reg_count, slave_pos, vendor_id, product_code, 0x7010, base,
                       &off->out_motor[motor]);
    }

    regs[(*reg_count)++] = (ec_pdo_entry_reg_t){0,      slave_pos, vendor_id,          product_code,
                                                0x6000, 1,         &off->in_motor_num, NULL};
    regs[(*reg_count)++] = (ec_pdo_entry_reg_t){0,      slave_pos, vendor_id,        product_code,
                                                0x6000, 2,         &off->in_can_ide, NULL};
    for (size_t motor = 0; motor < motor_slots; ++motor) {
        const uint8_t base = (uint8_t) (3 + (motor * EC_PDO_FIELDS_PER_MOTOR));
        add_motor_regs(regs, reg_count, slave_pos, vendor_id, product_code, 0x6000, base,
                       &off->in_motor[motor]);
    }

    return true;
}

/**
 * @brief 配置 CAN FD 从站的 PDO
 * @param[in,out] master 主站句柄
 * @param[in,out] slave_config 从站配置
 * @param[in,out] regs PDO 注册表
 * @param[in,out] reg_count 注册数量
 * @param[in] slave 从站位置
 * @return 成功返回 true，失败返回 false
 */
static bool configure_can_fd_slave(EcMaster* master, ec_slave_config_t* slave_config,
                                   ec_pdo_entry_reg_t* regs, size_t* reg_count, size_t slave) {
    const EcSlavePdoLayout* layout = &master->layout.slaves[slave];
    ec_pdo_entry_info_t out_entries[8][EC_CAN_FD_PDO_ENTRIES];
    ec_pdo_entry_info_t in_entries[8][EC_CAN_FD_PDO_ENTRIES];
    ec_pdo_info_t out_pdos[8];
    ec_pdo_info_t in_pdos[8];
    const unsigned int pdo_entries =
        (unsigned int) (layout->slots_per_bus * EC_PDO_FIELDS_PER_MOTOR);

    for (size_t bus = 0; bus < layout->bus_count; ++bus) {
        const uint16_t out_index = (uint16_t) (0x7010 + bus);
        const uint16_t in_index = (uint16_t) (0x6000 + bus);
        make_can_fd_pdo_entries(out_index, layout->slots_per_bus, out_entries[bus]);
        make_can_fd_pdo_entries(in_index, layout->slots_per_bus, in_entries[bus]);
        out_pdos[bus] = (ec_pdo_info_t){(uint16_t) (0x1600 + bus), pdo_entries, out_entries[bus]};
        in_pdos[bus] = (ec_pdo_info_t){(uint16_t) (0x1a00 + bus), pdo_entries, in_entries[bus]};
    }

    ec_sync_info_t syncs[] = {
        {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
        {1, EC_DIR_INPUT, 0, NULL, EC_WD_DISABLE},
        {2, EC_DIR_OUTPUT, (unsigned int) layout->bus_count, out_pdos, EC_WD_ENABLE},
        {3, EC_DIR_INPUT, (unsigned int) layout->bus_count, in_pdos, EC_WD_DISABLE},
        {0xff, 0, 0, NULL, 0},
    };

    if (ecrt_slave_config_pdos(slave_config, EC_END, syncs) != 0) {
        fprintf(stderr, "ecrt_slave_config_pdos failed at slave %zu.\n", slave);
        return false;
    }

    const uint16_t slave_pos = (uint16_t) slave;
    const uint32_t vendor_id = master->vendor_ids[slave];
    const uint32_t product_code = master->product_codes[slave];
    EcSlaveOffsets* off = &master->offsets[slave];
    for (size_t bus = 0; bus < layout->bus_count; ++bus) {
        const uint16_t out_index = (uint16_t) (0x7010 + bus);
        const uint16_t in_index = (uint16_t) (0x6000 + bus);
        for (size_t motor = 0; motor < layout->slots_per_bus; ++motor) {
            const size_t slot = (bus * layout->slots_per_bus) + motor;
            const uint8_t base = (uint8_t) (1 + (motor * EC_PDO_FIELDS_PER_MOTOR));
            add_motor_regs(regs, reg_count, slave_pos, vendor_id, product_code, out_index, base,
                           &off->out_motor[slot]);
            add_motor_regs(regs, reg_count, slave_pos, vendor_id, product_code, in_index, base,
                           &off->in_motor[slot]);
        }
    }

    return true;
}

/**
 * @brief 配置域的 PDO Entry 注册
 * @param[in,out] master 主站句柄
 * @return 成功返回 true，失败返回 false
 */
static bool configure_domain_entries(EcMaster* master) {
    ec_pdo_entry_reg_t* regs = calloc((size_t) EC_MAX_DOMAIN_REGS + 1U, sizeof(ec_pdo_entry_reg_t));
    if (regs == NULL) {
        fprintf(stderr, "Failed to allocate PDO registration table.\n");
        return false;
    }

    bool ok = true;
    size_t reg_count = 0;
    for (size_t slave = 0; slave < master->layout.slave_count && ok; ++slave) {
        const EcSlavePdoLayout* layout = &master->layout.slaves[slave];
        if (layout->format == EC_SLAVE_FORMAT_NONE) {
            continue;
        }

        ec_slave_config_t* slave_config =
            ecrt_master_slave_config(master->master, 0, (uint16_t) slave, master->vendor_ids[slave],
                                     master->product_codes[slave]);
        if (slave_config == NULL) {
            fprintf(stderr, "ecrt_master_slave_config failed at slave %zu.\n", slave);
            ok = false;
            break;
        }

        if (layout->format == EC_SLAVE_FORMAT_CLASSIC_CAN_2_BUS) {
            ok = configure_classic_slave(master, slave_config, regs, &reg_count, slave,
                                         EC_CLASSIC_CAN_2_BUS_SLOTS);
        } else {
            ok = configure_can_fd_slave(master, slave_config, regs, &reg_count, slave);
        }
    }

    if (ok && ecrt_domain_reg_pdo_entry_list(master->domain, regs) != 0) {
        fprintf(stderr, "ecrt_domain_reg_pdo_entry_list failed.\n");
        ok = false;
    }

    free(regs);
    return ok;
}

/**
 * @brief 打开 IgH EtherCAT 主站
 * @param[out] master 主站句柄
 * @param[in] master_index_text 主站索引字符串（如 "0"）
 * @return 成功返回 true，失败返回 false
 */
bool ec_master_open(EcMaster* master, const char* master_index_text) {
    if (master == NULL || master_index_text == NULL) {
        return false;
    }

    memset(master, 0, sizeof(*master));
    master->master_index = (unsigned int) strtoul(master_index_text, NULL, 0);

    char device_path[64];
    snprintf(device_path, sizeof(device_path), "/dev/EtherCAT%u", master->master_index);
    if (access(device_path, R_OK | W_OK) != 0) {
        fprintf(stderr,
                "IGH master device '%s' is not accessible. Start IGH master and check "
                "permissions.\n",
                device_path);
    }

    master->master = ecrt_request_master(master->master_index);
    if (master->master == NULL) {
        fprintf(stderr, "ecrt_request_master(%u) failed.\n", master->master_index);
        return false;
    }

    if (!configure_layouts(master)) {
        ec_master_close(master);
        return false;
    }

    master->domain = ecrt_master_create_domain(master->master);
    if (master->domain == NULL) {
        fprintf(stderr, "ecrt_master_create_domain failed.\n");
        ec_master_close(master);
        return false;
    }

    if (!configure_domain_entries(master)) {
        ec_master_close(master);
        return false;
    }

    if (ecrt_master_activate(master->master) != 0) {
        fprintf(stderr, "ecrt_master_activate failed.\n");
        ec_master_close(master);
        return false;
    }

    master->domain_pd = ecrt_domain_data(master->domain);
    if (master->domain_pd == NULL) {
        fprintf(stderr, "ecrt_domain_data returned null.\n");
        ec_master_close(master);
        return false;
    }

    master->initialized = true;
    return true;
}

/**
 * @brief 关闭 IgH EtherCAT 主站
 * @param[in,out] master 主站句柄
 */
void ec_master_close(EcMaster* master) {
    if (master == NULL) {
        return;
    }
    if (master->master != NULL) {
        ecrt_release_master(master->master);
    }
    master->master = NULL;
    master->domain = NULL;
    master->domain_pd = NULL;
    master->initialized = false;
}

/**
 * @brief 将报文写入域过程数据
 * @param[in,out] master 主站句柄
 * @param[in] offsets 偏移信息
 * @param[in] packet 要写入的报文
 */
static void write_packet_to_domain(EcMaster* master, const EcMotorOffsets* offsets,
                                   const MotorPackMsg* packet) {
    EC_WRITE_U32(master->domain_pd + offsets->id, packet->id);
    EC_WRITE_U8(master->domain_pd + offsets->frame_flags, packet->frame_flags);
    EC_WRITE_U8(master->domain_pd + offsets->dlc, packet->len);
    for (size_t i = 0; i < sizeof(packet->data); ++i) {
        EC_WRITE_U8(master->domain_pd + offsets->data[i], packet->data[i]);
    }
}

/**
 * @brief 从域过程数据读取报文
 * @param[in] master 主站句柄
 * @param[in] offsets 偏移信息
 * @param[out] packet 读取到的报文
 */
static void read_packet_from_domain(const EcMaster* master, const EcMotorOffsets* offsets,
                                    MotorPackMsg* packet) {
    packet->id = EC_READ_U32(master->domain_pd + offsets->id);
    packet->frame_flags = EC_READ_U8(master->domain_pd + offsets->frame_flags);
    packet->len = EC_READ_U8(master->domain_pd + offsets->dlc);
    for (size_t i = 0; i < sizeof(packet->data); ++i) {
        packet->data[i] = EC_READ_U8(master->domain_pd + offsets->data[i]);
    }
}

/**
 * @brief 发送电机报文到指定槽位
 * @param[in,out] master 主站句柄
 * @param[in] config 电机配置
 * @param[in] slot 槽位编号
 * @param[in] packet 要发送的报文
 * @return 成功返回 true，失败返回 false
 */
bool ec_master_send_packet(EcMaster* master, const MotorConfig* config, uint16_t slot,
                           const MotorPackMsg* packet) {
    if (master == NULL || config == NULL || packet == NULL || !master->initialized) {
        return false;
    }
    if (!ec_validate_target(&master->layout, config->slaveId, config->busId, slot)) {
        fprintf(stderr, "Invalid target: slaveId=%u busId=%u slot=%u\n", config->slaveId,
                config->busId, slot);
        return false;
    }

    const EcSlavePdoLayout* layout = &master->layout.slaves[config->slaveId];
    const size_t motor_slot = ((size_t) config->busId * layout->slots_per_bus) + slot;
    EcSlaveOffsets* slave_offsets = &master->offsets[config->slaveId];

    if (layout->format == EC_SLAVE_FORMAT_CLASSIC_CAN_2_BUS) {
        const uint8_t current_count = EC_READ_U8(master->domain_pd + slave_offsets->out_motor_num);
        if (current_count < layout_slot_count(layout)) {
            EC_WRITE_U8(master->domain_pd + slave_offsets->out_motor_num, current_count + 1U);
        }
        if ((packet->frame_flags & MOTOR_CAN_FLAG_EFF) != 0) {
            EC_WRITE_U8(master->domain_pd + slave_offsets->out_can_ide, 1);
        }
    }

    write_packet_to_domain(master, &slave_offsets->out_motor[motor_slot], packet);
    return true;
}

/**
 * @brief 清空所有从站的输出数据
 * @param[in,out] master 主站句柄
 */
static void clear_outputs(EcMaster* master) {
    for (size_t slave = 0; slave < master->layout.slave_count; ++slave) {
        const EcSlavePdoLayout* layout = &master->layout.slaves[slave];
        if (layout->format == EC_SLAVE_FORMAT_NONE) {
            continue;
        }

        EcSlaveOffsets* offsets = &master->offsets[slave];
        if (layout->format == EC_SLAVE_FORMAT_CLASSIC_CAN_2_BUS) {
            EC_WRITE_U8(master->domain_pd + offsets->out_motor_num, 0);
            EC_WRITE_U8(master->domain_pd + offsets->out_can_ide, 0);
        }
        const size_t slot_count = layout_slot_count(layout);
        for (size_t slot = 0; slot < slot_count; ++slot) {
            MotorPackMsg empty = {0};
            write_packet_to_domain(master, &offsets->out_motor[slot], &empty);
        }
    }
}

/**
 * @brief 检查并打印主站和域的状态变化
 * @param[in,out] master 主站句柄
 */
static void check_states(EcMaster* master) {
    ec_domain_state_t domain_state = {0};
    ecrt_domain_state(master->domain, &domain_state);
    if (domain_state.working_counter != master->domain_state.working_counter ||
        domain_state.wc_state != master->domain_state.wc_state) {
        printf("IGH domain wc=%u state=%u\n", domain_state.working_counter,
               (unsigned int) domain_state.wc_state);
    }
    master->domain_state = domain_state;
    master->last_working_counter = domain_state.working_counter;

    ec_master_state_t master_state = {0};
    ecrt_master_state(master->master, &master_state);
    if (master_state.slaves_responding != master->master_state.slaves_responding ||
        master_state.al_states != master->master_state.al_states ||
        master_state.link_up != master->master_state.link_up) {
        printf("IGH master slaves=%u al=0x%02x link=%u\n", master_state.slaves_responding,
               (unsigned int) master_state.al_states, (unsigned int) master_state.link_up);
    }
    master->master_state = master_state;
}

/**
 * @brief 执行一次 EtherCAT 周期循环（接收、处理、发送）
 * @param[in,out] master 主站句柄
 * @return 工作计数器完整返回 true，否则返回 false
 */
bool ec_master_cycle(EcMaster* master) {
    if (master == NULL || !master->initialized) {
        return false;
    }

    ecrt_master_receive(master->master);
    ecrt_domain_process(master->domain);
    check_states(master);

    for (size_t slave = 0; slave < master->layout.slave_count; ++slave) {
        const EcSlavePdoLayout* layout = &master->layout.slaves[slave];
        if (layout->format == EC_SLAVE_FORMAT_NONE) {
            continue;
        }
        const EcSlaveOffsets* offsets = &master->offsets[slave];
        for (uint16_t bus = 0; bus < layout->bus_count; ++bus) {
            for (uint16_t slot = 0; slot < layout->slots_per_bus; ++slot) {
                const size_t motor_slot = ((size_t) bus * layout->slots_per_bus) + slot;
                MotorPackMsg packet = {0};
                read_packet_from_domain(master, &offsets->in_motor[motor_slot], &packet);
                if (packet.len != 0 &&
                    external_device_process_packet(&master->external_devices[slave][bus],
                                                   &packet) == EXTERNAL_DEVICE_FRAME_MOTOR) {
                    printf("rx slaveId=%zu busId=%u slot=%u ", slave, bus, slot);
                    motor_print_received_packet(&packet, 30.0f);
                }
            }
        }
    }

    ecrt_domain_queue(master->domain);
    ecrt_master_send(master->master);
    clear_outputs(master);
    return master->domain_state.wc_state == EC_WC_COMPLETE;
}

/**
 * @brief 打印主站布局信息
 * @param[in] master 主站句柄
 */
void ec_print_layout(const EcMaster* master) {
    if (master == NULL) {
        return;
    }
    for (size_t slave = 0; slave < master->layout.slave_count; ++slave) {
        const EcSlavePdoLayout* layout = &master->layout.slaves[slave];
        printf("slaveId=%zu ighSlave=%zu format=%s busCount=%zu slotsPerBus=%zu pdo=%zu\n", slave,
               slave, format_name(layout->format), layout->bus_count, layout->slots_per_bus,
               layout->pdo_size);
    }
}
