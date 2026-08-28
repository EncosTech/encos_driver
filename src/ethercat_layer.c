#include "example_soem/ethercat_layer.h"

#include <stdio.h>
#include <string.h>

enum {
    EC_CLASSIC_CAN_2_BUS_SIZE = 86,
    EC_CAN_FD_3_BUS_SIZE = 336,
    EC_CAN_FD_8_BUS_SIZE = 896,
    EC_CAN_FD_8_BUS_10_SLOTS_SIZE = 1120,
    EC_MAX_LAYOUT_SLAVES = 16,
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
        if (*motor_num < layout->bus_count * layout->slots_per_bus) {
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
 * @brief 配置所有从站的 PDO 布局
 * @param[in,out] master 主站句柄
 * @return 成功返回 true，失败返回 false
 */
static bool configure_layouts(EcMaster* master) {
    if (master->ctx.slavecount <= 0) {
        fprintf(stderr, "No EtherCAT slaves found.\n");
        return false;
    }

    const size_t slave_count = (size_t) master->ctx.slavecount;
    if (slave_count > EC_MAX_LAYOUT_SLAVES) {
        fprintf(stderr, "Too many slaves: %zu, max supported: %d\n", slave_count,
                EC_MAX_LAYOUT_SLAVES);
        return false;
    }

    master->layout.slave_count = slave_count;
    for (size_t i = 0; i < slave_count; ++i) {
        const int soem_slave = (int) i + 1;
        const size_t obytes = (size_t) master->ctx.slavelist[soem_slave].Obytes;
        EcSlavePdoLayout* layout = &master->layout.slaves[i];
        if (ec_identify_pdo_layout(obytes, layout)) {
            printf("slaveId=%zu soemSlave=%d format=%s busCount=%zu slotsPerBus=%zu pdo=%zu\n", i,
                   soem_slave, format_name(layout->format), layout->bus_count,
                   layout->slots_per_bus, layout->pdo_size);
        } else {
            printf("slaveId=%zu soemSlave=%d unsupported pdo output bytes=%zu\n", i, soem_slave,
                   obytes);
        }
    }

    return true;
}

/**
 * @brief 打开 SOEM EtherCAT 主站
 * @param[out] master 主站句柄
 * @param[in] ifname 网卡接口名（如 "eth0"）
 * @return 成功返回 true，失败返回 false
 */
bool ec_master_open(EcMaster* master, const char* ifname) {
    if (master == NULL || ifname == NULL) {
        return false;
    }

    memset(master, 0, sizeof(*master));
    if (ecx_init(&master->ctx, ifname) <= 0) {
        fprintf(stderr, "ecx_init failed on interface '%s'. Try running as root or setcap.\n",
                ifname);
        return false;
    }

    if (ecx_config_init(&master->ctx) <= 0) {
        fprintf(stderr, "No EtherCAT slaves found on '%s'.\n", ifname);
        ecx_close(&master->ctx);
        return false;
    }

    ecx_config_map_group(&master->ctx, master->io_map, 0);
    ecx_configdc(&master->ctx);

    if (!configure_layouts(master)) {
        ecx_close(&master->ctx);
        return false;
    }

    ecx_statecheck(&master->ctx, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);

    master->ctx.slavelist[0].state = EC_STATE_OPERATIONAL;
    ecx_send_processdata(&master->ctx);
    ecx_receive_processdata(&master->ctx, EC_TIMEOUTRET);
    ecx_writestate(&master->ctx, 0);

    int retries = 40;
    while (retries-- > 0 && master->ctx.slavelist[0].state != EC_STATE_OPERATIONAL) {
        ecx_send_processdata(&master->ctx);
        ecx_receive_processdata(&master->ctx, EC_TIMEOUTRET);
        ecx_statecheck(&master->ctx, 0, EC_STATE_OPERATIONAL, 50000);
    }

    if (master->ctx.slavelist[0].state != EC_STATE_OPERATIONAL) {
        fprintf(stderr, "Not all slaves reached OPERATIONAL state.\n");
        ecx_close(&master->ctx);
        return false;
    }

    master->expected_wkc =
        (master->ctx.grouplist[0].outputsWKC * 2) + master->ctx.grouplist[0].inputsWKC;
    master->initialized = true;
    return true;
}

/**
 * @brief 关闭 SOEM EtherCAT 主站
 * @param[in,out] master 主站句柄
 */
void ec_master_close(EcMaster* master) {
    if (master != NULL && master->initialized) {
        ecx_close(&master->ctx);
        master->initialized = false;
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

    const int soem_slave = (int) config->slaveId + 1;
    uint8_t* outputs = (uint8_t*) master->ctx.slavelist[soem_slave].outputs;
    if (outputs == NULL) {
        fprintf(stderr, "Slave %u has no output buffer.\n", config->slaveId);
        return false;
    }

    return ec_write_motor_packet(outputs, &master->layout.slaves[config->slaveId], config->busId,
                                 slot, packet);
}

/**
 * @brief 执行一次 EtherCAT 周期循环（发送、接收、读取回包）
 * @param[in,out] master 主站句柄
 * @return 工作计数器完整返回 true，否则返回 false
 */
bool ec_master_cycle(EcMaster* master) {
    if (master == NULL || !master->initialized) {
        return false;
    }

    ecx_send_processdata(&master->ctx);
    master->last_wkc = ecx_receive_processdata(&master->ctx, EC_TIMEOUTRET);
    if (master->last_wkc < master->expected_wkc) {
        fprintf(stderr, "Bad WKC: got %d expected %d\n", master->last_wkc, master->expected_wkc);
    }

    for (size_t slave = 0; slave < master->layout.slave_count; ++slave) {
        const EcSlavePdoLayout* layout = &master->layout.slaves[slave];
        const uint8_t* inputs = (const uint8_t*) master->ctx.slavelist[slave + 1].inputs;
        if (inputs == NULL || layout->format == EC_SLAVE_FORMAT_NONE) {
            continue;
        }
        for (uint16_t bus = 0; bus < layout->bus_count; ++bus) {
            for (uint16_t slot = 0; slot < layout->slots_per_bus; ++slot) {
                MotorPackMsg packet = {0};
                if (ec_read_motor_packet(inputs, layout, bus, slot, &packet) &&
                    external_device_process_packet(&master->external_devices[slave][bus],
                                                   &packet) == EXTERNAL_DEVICE_FRAME_MOTOR) {
                    printf("rx slaveId=%zu busId=%u slot=%u ", slave, bus, slot);
                    motor_print_received_packet(&packet, 30.0f);
                }
            }
        }
    }

    return master->last_wkc >= master->expected_wkc;
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
        printf("slaveId=%zu soemSlave=%zu format=%s busCount=%zu slotsPerBus=%zu pdo=%zu\n", slave,
               slave + 1, format_name(layout->format), layout->bus_count, layout->slots_per_bus,
               layout->pdo_size);
    }
}
