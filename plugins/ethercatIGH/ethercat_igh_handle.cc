#include "ethercat_igh_handle.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "platform/delay.h"
#include "utils/tracy.h"

namespace {
constexpr std::size_t kClassicCan2BusMotorSlots = 6;
constexpr std::size_t kClassicCan8BusMotorSlots = 24;
constexpr std::size_t kMaxCanFdBusCount = 8;
constexpr std::size_t kMaxCanFdMotorsPerBus = 10;
constexpr std::size_t kMaxCanFdMotorSlots = kMaxCanFdBusCount * kMaxCanFdMotorsPerBus;
constexpr std::size_t kMaxPdoEntriesPerMapping = 255;
constexpr auto kSlaveStateLogPeriod = std::chrono::milliseconds(50);
constexpr std::size_t kClassicCan8BusMappedBytes =
    2 + (kClassicCan8BusMotorSlots * sizeof(MotorPackMsg));  // without reserved[2]

const char* al_state_to_string(unsigned int al_state) {
    switch (al_state) {
        case EC_AL_STATE_INIT:
            return "INIT";
        case EC_AL_STATE_PREOP:
            return "PREOP";
        case EC_AL_STATE_SAFEOP:
            return "SAFEOP";
        case EC_AL_STATE_OP:
            return "OP";
        default:
            return "UNKNOWN";
    }
}

const char* wc_state_to_string(ec_wc_state_t wc_state) {
    switch (wc_state) {
        case EC_WC_ZERO:
            return "ZERO";
        case EC_WC_INCOMPLETE:
            return "INCOMPLETE";
        case EC_WC_COMPLETE:
            return "COMPLETE";
        default:
            return "UNKNOWN";
    }
}

// 通过主站当前扫描到的 PDO 信息统计某个 slave 的输出 PDO 字节数。
// 这里等价于 SOEM 中用 Obytes 判定帧格式：输出区大小匹配普通 CAN 或 CAN FD 布局即采用对应格式。
std::size_t detect_output_bytes(ec_master_t* master, uint16_t slave_pos, uint8_t sync_count) {
    std::size_t output_bits = 0;
    for (uint8_t sync = 0; sync < sync_count; ++sync) {
        ec_sync_info_t sync_info{};
        if (ecrt_master_get_sync_manager(master, slave_pos, sync, &sync_info) != 0) {
            continue;
        }

        if (sync_info.dir != EC_DIR_OUTPUT || sync_info.n_pdos == 0) {
            continue;
        }

        for (uint16_t pdo_pos = 0; pdo_pos < sync_info.n_pdos; ++pdo_pos) {
            ec_pdo_info_t pdo_info{};
            if (ecrt_master_get_pdo(master, slave_pos, sync, pdo_pos, &pdo_info) != 0) {
                continue;
            }
            for (uint16_t entry_pos = 0; entry_pos < pdo_info.n_entries; ++entry_pos) {
                ec_pdo_entry_info_t entry{};
                if (ecrt_master_get_pdo_entry(master, slave_pos, sync, pdo_pos, entry_pos,
                                              &entry) == 0) {
                    output_bits += entry.bit_length;
                }
            }
        }
    }

    return (output_bits + 7U) / 8U;
}

// AX58100 的 0x7010/0x6000 PDO 模式是：
// [motor_num, can_ide] + N 组电机字段，每组 11 个 subindex：
// [id(32), frame_flags(8), dlc(8), data0..7(8*8)]。普通 CAN 可以是 6 槽位或 24 槽位。
std::vector<ec_pdo_entry_info_t> make_pdo_entries(uint16_t index, std::size_t motor_slots) {
    const std::size_t entry_count = 2 + (motor_slots * 11);
    std::vector<ec_pdo_entry_info_t> entries(entry_count);
    for (std::size_t i = 0; i < entry_count; ++i) {
        const uint8_t subindex = static_cast<uint8_t>(i + 1);
        const uint8_t bit_length = ((i >= 2) && ((i - 2) % 11 == 0)) ? 32 : 8;
        entries[i] = ec_pdo_entry_info_t{index, subindex, bit_length};
    }
    return entries;
}

std::vector<ec_pdo_entry_info_t> make_can_fd_pdo_entries(uint16_t index,
                                                         std::size_t motors_per_bus) {
    const std::size_t entry_count = motors_per_bus * 11;
    std::vector<ec_pdo_entry_info_t> entries(entry_count);
    for (std::size_t i = 0; i < entry_count; ++i) {
        const uint8_t subindex = static_cast<uint8_t>(i + 1);
        const uint8_t bit_length = (i % 11 == 0) ? 32 : 8;
        entries[i] = ec_pdo_entry_info_t{index, subindex, bit_length};
    }
    return entries;
}

// AX58100 的固定 PDO 以最多 255 个 entry 连续切分，不按 CAN 总线边界切分。
std::vector<std::vector<ec_pdo_entry_info_t>> split_pdo_entries(
    const std::vector<ec_pdo_entry_info_t>& entries) {
    std::vector<std::vector<ec_pdo_entry_info_t>> groups;
    groups.reserve((entries.size() + kMaxPdoEntriesPerMapping - 1) / kMaxPdoEntriesPerMapping);
    for (std::size_t offset = 0; offset < entries.size(); offset += kMaxPdoEntriesPerMapping) {
        const std::size_t count = std::min(kMaxPdoEntriesPerMapping, entries.size() - offset);
        groups.emplace_back(entries.begin() + static_cast<std::ptrdiff_t>(offset),
                            entries.begin() + static_cast<std::ptrdiff_t>(offset + count));
    }
    return groups;
}

std::vector<ec_pdo_info_t> make_pdo_infos(
    uint16_t first_pdo_index, std::vector<std::vector<ec_pdo_entry_info_t>>& entry_groups) {
    std::vector<ec_pdo_info_t> pdo_infos;
    pdo_infos.reserve(entry_groups.size());
    for (std::size_t pdo_pos = 0; pdo_pos < entry_groups.size(); ++pdo_pos) {
        auto& entries = entry_groups[pdo_pos];
        pdo_infos.push_back({static_cast<uint16_t>(first_pdo_index + pdo_pos),
                             static_cast<unsigned int>(entries.size()), entries.data()});
    }
    return pdo_infos;
}

}  // namespace

EthercatIGHHandle::EthercatIGHHandle(std::string interface_name, LoggerPtr logger)
    : EthercatBaseHandle(std::move(logger)), interface_name_(std::move(interface_name)) {
    if (interface_name_.empty() ||
        !std::all_of(interface_name_.begin(), interface_name_.end(), [](unsigned char character) {
            return std::isdigit(character) != 0;
        })) {
        throw std::invalid_argument("EthercatIGH requires a decimal master ID, got '" +
                                    interface_name_ + "'.");
    }
    try {
        master_index_ = static_cast<unsigned int>(std::stoul(interface_name_));
    } catch (const std::exception&) {
        throw std::invalid_argument("EthercatIGH master ID is out of range: '" + interface_name_ +
                                    "'.");
    }

    try {
        if (!Initialize()) {
            throw std::runtime_error("Failed to Initialize IGH EtherCAT master");
        }
    } catch (...) {
        Stop();
        Release();
        throw;
    }
}

EthercatIGHHandle::~EthercatIGHHandle() {
    Stop();
    Release();
}

bool EthercatIGHHandle::Initialize() {
    const std::filesystem::path master_device =
        std::filesystem::path("/dev") / ("EtherCAT" + std::to_string(master_index_));
    if (!std::filesystem::exists(master_device)) {
        logger_->error(
            "IGH master device '{}' not found. Please start IGH master first (for example: "
            "/etc/init.d/ethercat start).",
            master_device.string());
        return false;
    }

    // 1) 申请 master
    master_ = ecrt_request_master(master_index_);
    if (!master_) {
        logger_->error(
            "ecrt_request_master({}) failed. Please ensure IGH master is running and you have "
            "permissions for '{}'.",
            master_index_, master_device.string());
        return false;
    }

    if (access(master_device.c_str(), R_OK | W_OK) != 0) {
        logger_->warn("Device '{}' exists but current user may not have Read/write permission.",
                      master_device.string());
    }

    // 读取主站实时扫描到的 slave 数量，直接作为本 handle 的从站数量来源。
    // 不再使用环境变量手动覆盖。
    ec_master_info_t master_info{};
    if (ecrt_master(master_, &master_info) != 0) {
        logger_->error("Failed to query IGH master info.");
        return false;
    }
    if (master_info.slave_count == 0) {
        logger_->error("No slaves found on IGH master {}.", master_index_);
        return false;
    }
    slave_count_ = static_cast<std::size_t>(master_info.slave_count);

    // 按每个 slave 的实时 PDO 大小建立独立配置，避免把 slave0 的格式错误套用到其他 slave。
    ConfigureSlaveConfigs();

    // 2) 创建 process data domain
    domain_ = ecrt_master_create_domain(master_);
    if (!domain_) {
        logger_->error("ecrt_master_create_domain failed.");
        return false;
    }

    // 3) 建立 slave PDO 配置 + domain entry 注册
    if (!ConfigureDomainEntries()) {
        return false;
    }

    // 4) 激活 master 并获取 process data 内存基址
    if (ecrt_master_activate(master_)) {
        logger_->error("ecrt_master_activate failed.");
        return false;
    }

    domain_pd_ = ecrt_domain_data(domain_);
    if (!domain_pd_) {
        logger_->error("ecrt_domain_data returned null.");
        return false;
    }

    outputs_cleared_ = false;
    in_operational_.store(true);
    running_.store(true);
    return true;
}

bool EthercatIGHHandle::ConfigureDomainEntries() {
    slave_offsets_.clear();
    slave_offsets_.resize(slave_count_);
    slave_config_handles_.assign(slave_count_, nullptr);
    slave_states_.assign(slave_count_, {});
    slave_reached_operational_.assign(slave_count_, false);

    std::vector<ec_pdo_entry_reg_t> regs;
    regs.reserve((kMaxCanFdMotorSlots * 11) * 2 * slave_count_ + 1);

    for (std::size_t slave = 0; slave < slave_count_; ++slave) {
        if (slave >= slave_configs_.size()) {
            logger_->error("Missing slave config for slave {}.", slave);
            return false;
        }

        const auto& config = slave_configs_[slave];
        if (!HasSupportedPdo(config)) {
            continue;
        }

        const auto vendor_id =
            slave < slave_vendor_ids_.size() ? slave_vendor_ids_[slave] : kSlaveVendorId;
        const auto product_code =
            slave < slave_product_codes_.size() ? slave_product_codes_[slave] : kSlaveProductCode;

        // 对每个期望 slave 建立配置（alias=0，position=slave）。
        auto* sc = ecrt_master_slave_config(master_, 0, static_cast<uint16_t>(slave), vendor_id,
                                            product_code);
        if (!sc) {
            logger_->error("ecrt_master_slave_config failed at slave {}.", slave);
            return false;
        }
        slave_config_handles_[slave] = sc;

        const bool is_can_fd = config.format == SlaveFormat::CanFd8Bus ||
                               config.format == SlaveFormat::CanFd8Bus10Slots ||
                               config.format == SlaveFormat::CanFd3Bus ||
                               config.format == SlaveFormat::Glove;
        const bool is_can_fd3_bus = config.format == SlaveFormat::CanFd3Bus;
        const bool is_classic_can2_bus = config.format == SlaveFormat::ClassicCan2Bus;
        const bool is_classic_can8_bus = config.format == SlaveFormat::ClassicCan8Bus;
        const std::size_t can_fd_bus_count = is_can_fd ? config.bus_count : 0;
        const std::size_t motor_slots =
            is_can_fd
                ? (can_fd_bus_count * config.motors_per_bus)
                : (is_classic_can8_bus ? kClassicCan8BusMotorSlots : kClassicCan2BusMotorSlots);

        // 输出/输入 PDO 条目与参考工程保持一致：0x7010 / 0x6000。
        // 旧格式按当前 slave 的格式动态选择 6 槽位或 24 槽位。
        auto pdo_entries_out =
            is_can_fd ? std::vector<ec_pdo_entry_info_t>{} : make_pdo_entries(0x7010, motor_slots);
        auto pdo_entries_in =
            is_can_fd ? std::vector<ec_pdo_entry_info_t>{} : make_pdo_entries(0x6000, motor_slots);
        std::vector<std::vector<ec_pdo_entry_info_t>> mapped_pdo_entries_out;
        std::vector<std::vector<ec_pdo_entry_info_t>> mapped_pdo_entries_in;
        std::vector<ec_pdo_info_t> mapped_pdos_out;
        std::vector<ec_pdo_info_t> mapped_pdos_in;
        if (is_can_fd) {
            std::vector<ec_pdo_entry_info_t> all_pdo_entries_out;
            std::vector<ec_pdo_entry_info_t> all_pdo_entries_in;
            const std::size_t total_entry_count = can_fd_bus_count * config.motors_per_bus * 11;
            all_pdo_entries_out.reserve(total_entry_count);
            all_pdo_entries_in.reserve(total_entry_count);
            for (std::size_t bus = 0; bus < can_fd_bus_count; ++bus) {
                const auto out_index =
                    static_cast<uint16_t>((is_can_fd3_bus ? 0x7000 : 0x7010) + bus);
                const auto in_index = static_cast<uint16_t>(0x6000 + bus);
                auto out_entries = make_can_fd_pdo_entries(out_index, config.motors_per_bus);
                auto in_entries = make_can_fd_pdo_entries(in_index, config.motors_per_bus);
                all_pdo_entries_out.insert(all_pdo_entries_out.end(), out_entries.begin(),
                                           out_entries.end());
                all_pdo_entries_in.insert(all_pdo_entries_in.end(), in_entries.begin(),
                                          in_entries.end());
            }
            mapped_pdo_entries_out = split_pdo_entries(all_pdo_entries_out);
            mapped_pdo_entries_in = split_pdo_entries(all_pdo_entries_in);
            mapped_pdos_out = make_pdo_infos(0x1600, mapped_pdo_entries_out);
            mapped_pdos_in = make_pdo_infos(0x1a00, mapped_pdo_entries_in);
        } else {
            mapped_pdo_entries_out = split_pdo_entries(pdo_entries_out);
            mapped_pdo_entries_in = split_pdo_entries(pdo_entries_in);
            mapped_pdos_out =
                make_pdo_infos(is_classic_can2_bus ? 0x1600 : 0x1601, mapped_pdo_entries_out);
            mapped_pdos_in = make_pdo_infos(0x1a00, mapped_pdo_entries_in);
        }
        ec_pdo_entry_info_t pdo_entries_in_meta[] = {
            {0x6020, 0x01, 1}, {0x6020, 0x02, 1},  {0x6020, 0x03, 2},
            {0x6020, 0x05, 2}, {0x0000, 0x00, 8},  {0x1802, 0x07, 1},
            {0x1802, 0x09, 1}, {0x6020, 0x0b, 16}, {0x6020, 0x0c, 16},
        };

        if (!is_can_fd && !is_classic_can2_bus) {
            mapped_pdos_in.push_back({0x1a02,
                                      static_cast<unsigned int>(sizeof(pdo_entries_in_meta) /
                                                                sizeof(pdo_entries_in_meta[0])),
                                      pdo_entries_in_meta});
        }

        ec_sync_info_t slave_syncs[] = {
            {0, EC_DIR_OUTPUT, 0, nullptr, EC_WD_DISABLE},
            {1, EC_DIR_INPUT, 0, nullptr, EC_WD_DISABLE},
            {2, EC_DIR_OUTPUT, static_cast<unsigned int>(mapped_pdos_out.size()),
             mapped_pdos_out.data(), EC_WD_ENABLE},
            {3, EC_DIR_INPUT, static_cast<unsigned int>(mapped_pdos_in.size()),
             mapped_pdos_in.data(), EC_WD_DISABLE},
            {0xff},
        };

        if (ecrt_slave_config_pdos(sc, EC_END, slave_syncs)) {
            logger_->error("ecrt_slave_config_pdos failed at slave {}.", slave);
            return false;
        }

        auto& off = slave_offsets_[slave];
        off.out_motor.resize(motor_slots);
        off.in_motor.resize(motor_slots);

        if (is_can_fd) {
            for (std::size_t bus = 0; bus < can_fd_bus_count; ++bus) {
                const auto out_index =
                    static_cast<uint16_t>((is_can_fd3_bus ? 0x7000 : 0x7010) + bus);
                const auto in_index = static_cast<uint16_t>(0x6000 + bus);
                for (std::size_t motor = 0; motor < config.motors_per_bus; ++motor) {
                    const auto slot = (bus * config.motors_per_bus) + motor;
                    const uint8_t base = static_cast<uint8_t>(1 + (motor * 11));

                    auto& out = off.out_motor[slot];
                    regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code,
                                    out_index, base, &out.id, nullptr});
                    regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code,
                                    out_index, static_cast<uint8_t>(base + 1), &out.frame_flags,
                                    nullptr});
                    regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code,
                                    out_index, static_cast<uint8_t>(base + 2), &out.dlc, nullptr});
                    for (std::size_t j = 0; j < out.data.size(); ++j) {
                        regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code,
                                        out_index, static_cast<uint8_t>(base + 3 + j), &out.data[j],
                                        nullptr});
                    }

                    auto& in = off.in_motor[slot];
                    regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code,
                                    in_index, base, &in.id, nullptr});
                    regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code,
                                    in_index, static_cast<uint8_t>(base + 1), &in.frame_flags,
                                    nullptr});
                    regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code,
                                    in_index, static_cast<uint8_t>(base + 2), &in.dlc, nullptr});
                    for (std::size_t j = 0; j < in.data.size(); ++j) {
                        regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code,
                                        in_index, static_cast<uint8_t>(base + 3 + j), &in.data[j],
                                        nullptr});
                    }
                }
            }
            continue;
        }

        // 注册 OUT 头字段：motor_num / can_ide
        regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code, 0x7010, 1,
                        &off.out_motor_num, nullptr});
        regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code, 0x7010, 2,
                        &off.out_can_ide, nullptr});

        for (std::size_t motor = 0; motor < motor_slots; ++motor) {
            // 每个电机槽位占 11 个 subindex，base=3+11*m
            const uint8_t base = static_cast<uint8_t>(3 + (motor * 11));
            auto& mo = off.out_motor[motor];
            regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code, 0x7010, base,
                            &mo.id, nullptr});
            regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code, 0x7010,
                            static_cast<uint8_t>(base + 1), &mo.frame_flags, nullptr});
            regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code, 0x7010,
                            static_cast<uint8_t>(base + 2), &mo.dlc, nullptr});
            for (std::size_t j = 0; j < mo.data.size(); ++j) {
                regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code, 0x7010,
                                static_cast<uint8_t>(base + 3 + j), &mo.data[j], nullptr});
            }
        }

        // 注册 IN 头字段：motor_num / can_ide
        regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code, 0x6000, 1,
                        &off.in_motor_num, nullptr});
        regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code, 0x6000, 2,
                        &off.in_can_ide, nullptr});

        for (std::size_t motor = 0; motor < motor_slots; ++motor) {
            const uint8_t base = static_cast<uint8_t>(3 + (motor * 11));
            auto& mo = off.in_motor[motor];
            regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code, 0x6000, base,
                            &mo.id, nullptr});
            regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code, 0x6000,
                            static_cast<uint8_t>(base + 1), &mo.frame_flags, nullptr});
            regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code, 0x6000,
                            static_cast<uint8_t>(base + 2), &mo.dlc, nullptr});
            for (std::size_t j = 0; j < mo.data.size(); ++j) {
                regs.push_back({0, static_cast<uint16_t>(slave), vendor_id, product_code, 0x6000,
                                static_cast<uint8_t>(base + 3 + j), &mo.data[j], nullptr});
            }
        }
    }

    regs.push_back(ec_pdo_entry_reg_t{});
    // 一次性把所有条目注册进 domain，后续循环只按 offset 读写内存。
    if (ecrt_domain_reg_pdo_entry_list(domain_, regs.data())) {
        logger_->error("ecrt_domain_reg_pdo_entry_list failed.");
        return false;
    }

    return true;
}

void EthercatIGHHandle::ConfigureSlaveConfigs() {
    // 与 SOEM 路径保持一致：逐个 slave 按各自 PDO 大小建立配置。
    slave_configs_.clear();
    slave_configs_.resize(slave_count_);
    slave_vendor_ids_.assign(slave_count_, kSlaveVendorId);
    slave_product_codes_.assign(slave_count_, kSlaveProductCode);
    for (std::size_t slave = 0; slave < slave_count_; ++slave) {
        auto& cfg = slave_configs_[slave];
        ec_slave_info_t slave_info{};
        if (ecrt_master_get_slave(master_, static_cast<uint16_t>(slave), &slave_info) != 0) {
            logger_->warn("Failed to query IGH slave {} info; no Bus will be configured.", slave);
            continue;
        }

        slave_vendor_ids_[slave] = slave_info.vendor_id;
        slave_product_codes_[slave] = slave_info.product_code;

        const auto output_bytes =
            detect_output_bytes(master_, static_cast<uint16_t>(slave), slave_info.sync_count);
        const auto layout_bytes = output_bytes == kClassicCan8BusMappedBytes
                                      ? sizeof(EthercatClassicCanMsg8)
                                      : output_bytes;
        cfg = ClassifyOutputPdoSize(layout_bytes);
        if (HasSupportedPdo(cfg)) {
            logger_->info("IGH slave {} configured with {} Buses from {} output bytes.", slave,
                          cfg.bus_count, output_bytes);
        } else {
            logger_->warn(
                "IGH slave {} has unsupported output PDO bytes {}; no Bus will be "
                "configured.",
                slave, output_bytes);
        }
    }
}
void EthercatIGHHandle::CheckMasterState() {
    // 仅在状态变化时打日志，避免高频循环刷屏。
    ec_master_state_t state{};
    ecrt_master_state(master_, &state);
    if (state.slaves_responding != master_state_.slaves_responding) {
        logger_->info("IGH slaves responding changed: {} -> {}", master_state_.slaves_responding,
                      state.slaves_responding);
    }
    if (state.al_states != master_state_.al_states) {
        logger_->info("IGH AL state changed: 0x{:x} -> 0x{:x}",
                      static_cast<unsigned int>(master_state_.al_states),
                      static_cast<unsigned int>(state.al_states));
    }
    if (state.link_up != master_state_.link_up) {
        logger_->info("IGH link state changed: {} -> {}",
                      static_cast<unsigned int>(master_state_.link_up),
                      static_cast<unsigned int>(state.link_up));
    }
    master_state_ = state;
}

void EthercatIGHHandle::CheckDomainState() {
    // WC 变化通常可用于定位链路抖动或 PDO 映射问题。
    ec_domain_state_t state{};
    ecrt_domain_state(domain_, &state);
    if (state.working_counter != domain_state_.working_counter) {
        ENCOS_LOG_DEBUG(logger_, "IGH domain WC changed: {} -> {}", domain_state_.working_counter,
                        state.working_counter);
    }
    if (state.wc_state != domain_state_.wc_state) {
        if (state.wc_state == EC_WC_COMPLETE) {
            logger_->info("IGH domain WKC recovered: {} ({})", state.working_counter,
                          wc_state_to_string(state.wc_state));
        } else {
            logger_->error("Dropped packet (Bad WKC: {}, state: {})", state.working_counter,
                           wc_state_to_string(state.wc_state));
        }
    }
    domain_state_ = state;
}

void EthercatIGHHandle::CheckSlaveStates() {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_slave_state_check_ < kSlaveStateLogPeriod) {
        return;
    }
    last_slave_state_check_ = now;

    for (std::size_t slave = 0; slave < slave_config_handles_.size(); ++slave) {
        const auto* sc = slave_config_handles_[slave];
        if (!sc) {
            continue;
        }

        ec_slave_config_state_t state{};
        if (ecrt_slave_config_state(sc, &state) != 0) {
            logger_->warn("Failed to query IGH slave {} state.", slave);
            continue;
        }

        const auto& previous = slave_states_[slave];
        if (state.online == previous.online && state.operational == previous.operational &&
            state.al_state == previous.al_state) {
            continue;
        }

        const unsigned int online = state.online;
        const unsigned int operational = state.operational;
        const unsigned int al_state = state.al_state;
        if (!state.online) {
            logger_->error("IGH slave {} lost.", slave);
        } else if (!state.operational) {
            if (slave_reached_operational_[slave]) {
                logger_->error("IGH slave {} State=0x{:02x} ({}) Online={} Operational={}", slave,
                               al_state, al_state_to_string(al_state), online, operational);
            } else {
                logger_->info(
                    "IGH slave {} initializing: State=0x{:02x} ({}) Online={} "
                    "Operational={}",
                    slave, al_state, al_state_to_string(al_state), online, operational);
            }
        } else {
            logger_->info("IGH slave {} reached OPERATIONAL.", slave);
            slave_reached_operational_[slave] = true;
        }
        slave_states_[slave] = state;
    }
}

void EthercatIGHHandle::Stop() {
    running_.store(false);
    in_operational_.store(false);
}

void EthercatIGHHandle::Release() {
    std::lock_guard<std::mutex> lock(release_mutex_);
    if (master_) {
        ecrt_release_master(master_);
        master_ = nullptr;
    }
    domain_ = nullptr;
    domain_pd_ = nullptr;
}

void EthercatIGHHandle::WriteOutputs(const OutputFrame& packets) {
    // 把 base 层打包帧写入 domain process data。
    // 每个 slave 按各自配置选择普通 CAN 或 CAN FD 布局。
    outputs_cleared_ = false;
    for (std::size_t slave = 0; slave < slave_count_; ++slave) {
        if (slave >= slave_offsets_.size() || slave >= slave_configs_.size()) {
            continue;
        }

        const auto& config = slave_configs_[slave];
        if (!HasSupportedPdo(config)) {
            continue;
        }

        if (slave >= packets.size() || packets[slave].empty()) {
            ClearOutput(slave);
            continue;
        }

        const auto& packet_buf = packets[slave];
        const auto& off = slave_offsets_[slave];
        const std::size_t motor_slots = off.out_motor.size();
        if (config.format == SlaveFormat::CanFd8Bus ||
            config.format == SlaveFormat::CanFd8Bus10Slots ||
            config.format == SlaveFormat::CanFd3Bus || config.format == SlaveFormat::Glove) {
            if (packet_buf.size() < config.message_size) {
                ClearOutput(slave);
                continue;
            }
            const auto* motors = reinterpret_cast<const MotorPackMsg*>(packet_buf.data());
            for (std::size_t i = 0; i < motor_slots; ++i) {
                const auto& src = motors[i];
                const auto& dst = off.out_motor[i];
                EC_WRITE_U32(domain_pd_ + dst.id, src.id);
                EC_WRITE_U8(domain_pd_ + dst.frame_flags, src.frame_flags);
                EC_WRITE_U8(domain_pd_ + dst.dlc, src.len);
                for (std::size_t j = 0; j < sizeof(src.data); ++j) {
                    EC_WRITE_U8(domain_pd_ + dst.data[j], src.data[j]);
                }
            }
        } else if (config.format == SlaveFormat::ClassicCan8Bus) {
            if (packet_buf.size() < sizeof(EthercatClassicCanMsg8)) {
                ClearOutput(slave);
                continue;
            }
            const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(packet_buf.data());
            EC_WRITE_U8(domain_pd_ + off.out_can_ide, packet->can_ide);
            EC_WRITE_U8(domain_pd_ + off.out_motor_num, packet->motor_num);
            for (std::size_t i = 0; i < motor_slots; ++i) {
                const auto& src = packet->motor[i];
                const auto& dst = off.out_motor[i];
                EC_WRITE_U32(domain_pd_ + dst.id, src.id);
                EC_WRITE_U8(domain_pd_ + dst.frame_flags, src.frame_flags);
                EC_WRITE_U8(domain_pd_ + dst.dlc, src.len);
                for (std::size_t j = 0; j < sizeof(src.data); ++j) {
                    EC_WRITE_U8(domain_pd_ + dst.data[j], src.data[j]);
                }
            }
        } else if (config.format == SlaveFormat::ClassicCan2Bus) {
            if (packet_buf.size() < sizeof(EthercatClassicCanMsg2)) {
                ClearOutput(slave);
                continue;
            }
            const auto* packet = reinterpret_cast<const EthercatClassicCanMsg2*>(packet_buf.data());
            EC_WRITE_U8(domain_pd_ + off.out_can_ide, packet->can_ide);
            EC_WRITE_U8(domain_pd_ + off.out_motor_num, packet->motor_num);
            for (std::size_t i = 0; i < motor_slots; ++i) {
                const auto& src = packet->motor[i];
                const auto& dst = off.out_motor[i];
                EC_WRITE_U32(domain_pd_ + dst.id, src.id);
                EC_WRITE_U8(domain_pd_ + dst.frame_flags, src.frame_flags);
                EC_WRITE_U8(domain_pd_ + dst.dlc, src.len);
                for (std::size_t j = 0; j < sizeof(src.data); ++j) {
                    EC_WRITE_U8(domain_pd_ + dst.data[j], src.data[j]);
                }
            }
        } else {
            ClearOutput(slave);
        }
    }
}

void EthercatIGHHandle::ClearOutput(std::size_t slave) {
    if (slave >= slave_count_ || slave >= slave_offsets_.size() || slave >= slave_configs_.size() ||
        !HasSupportedPdo(slave_configs_[slave])) {
        return;
    }

    const auto& off = slave_offsets_[slave];
    const auto format =
        slave < slave_configs_.size() ? slave_configs_[slave].format : SlaveFormat::None;

    if (format != SlaveFormat::CanFd8Bus && format != SlaveFormat::CanFd8Bus10Slots &&
        format != SlaveFormat::CanFd3Bus && format != SlaveFormat::Glove) {
        EC_WRITE_U8(domain_pd_ + off.out_can_ide, 0);
        EC_WRITE_U8(domain_pd_ + off.out_motor_num, 0);
    }

    for (std::size_t i = 0; i < off.out_motor.size(); ++i) {
        const auto& dst = off.out_motor[i];
        EC_WRITE_U32(domain_pd_ + dst.id, 0);
        EC_WRITE_U8(domain_pd_ + dst.frame_flags, 0);
        EC_WRITE_U8(domain_pd_ + dst.dlc, 0);
        for (std::size_t j = 0; j < dst.data.size(); ++j) {
            EC_WRITE_U8(domain_pd_ + dst.data[j], 0);
        }
    }
}

void EthercatIGHHandle::ClearOutputs() {
    if (outputs_cleared_) {
        return;
    }

    for (std::size_t slave = 0; slave < slave_count_; ++slave) {
        ClearOutput(slave);
    }

    outputs_cleared_ = true;
}

MotorMessages EthercatIGHHandle::ReadInputs() {
    // 先从 domain 读出每个 slave 的原始帧，再交给 base 层统一解码为业务消息。
    // 普通 CAN / CAN FD 结构体大小不同，这里按当前 format 分配帧缓存。
    std::vector<std::vector<uint8_t>> raw_frames(slave_count_);
    std::vector<const uint8_t*> inputs;
    inputs.reserve(slave_count_);

    for (std::size_t slave = 0; slave < slave_count_; ++slave) {
        const auto& off = slave_offsets_[slave];
        if (slave >= slave_configs_.size()) {
            continue;
        }

        const auto& config = slave_configs_[slave];
        const std::size_t motor_slots = off.in_motor.size();
        if (config.format == SlaveFormat::CanFd8Bus ||
            config.format == SlaveFormat::CanFd8Bus10Slots ||
            config.format == SlaveFormat::CanFd3Bus || config.format == SlaveFormat::Glove) {
            raw_frames[slave].assign(config.message_size, 0);
            auto* motors = reinterpret_cast<MotorPackMsg*>(raw_frames[slave].data());
            for (std::size_t i = 0; i < motor_slots; ++i) {
                auto& dst = motors[i];
                const auto& src = off.in_motor[i];
                dst.id = EC_READ_U32(domain_pd_ + src.id);
                dst.frame_flags = EC_READ_U8(domain_pd_ + src.frame_flags);
                dst.len = EC_READ_U8(domain_pd_ + src.dlc);
                for (std::size_t j = 0; j < sizeof(dst.data); ++j) {
                    dst.data[j] = EC_READ_U8(domain_pd_ + src.data[j]);
                }
            }
            inputs.push_back(raw_frames[slave].data());
        } else if (config.format == SlaveFormat::ClassicCan8Bus) {
            raw_frames[slave].assign(sizeof(EthercatClassicCanMsg8), 0);
            auto* packet = reinterpret_cast<EthercatClassicCanMsg8*>(raw_frames[slave].data());
            packet->can_ide = EC_READ_U8(domain_pd_ + off.in_can_ide);
            packet->motor_num = EC_READ_U8(domain_pd_ + off.in_motor_num);
            for (std::size_t i = 0; i < motor_slots; ++i) {
                auto& dst = packet->motor[i];
                const auto& src = off.in_motor[i];
                dst.id = EC_READ_U32(domain_pd_ + src.id);
                dst.frame_flags = EC_READ_U8(domain_pd_ + src.frame_flags);
                dst.len = EC_READ_U8(domain_pd_ + src.dlc);
                for (std::size_t j = 0; j < sizeof(dst.data); ++j) {
                    dst.data[j] = EC_READ_U8(domain_pd_ + src.data[j]);
                }
            }
            inputs.push_back(reinterpret_cast<const uint8_t*>(packet));
        } else if (config.format == SlaveFormat::ClassicCan2Bus) {
            raw_frames[slave].assign(sizeof(EthercatClassicCanMsg2), 0);
            auto* packet = reinterpret_cast<EthercatClassicCanMsg2*>(raw_frames[slave].data());
            packet->can_ide = EC_READ_U8(domain_pd_ + off.in_can_ide);
            packet->motor_num = EC_READ_U8(domain_pd_ + off.in_motor_num);
            for (std::size_t i = 0; i < motor_slots; ++i) {
                auto& dst = packet->motor[i];
                const auto& src = off.in_motor[i];
                dst.id = EC_READ_U32(domain_pd_ + src.id);
                dst.frame_flags = EC_READ_U8(domain_pd_ + src.frame_flags);
                dst.len = EC_READ_U8(domain_pd_ + src.dlc);
                for (std::size_t j = 0; j < sizeof(dst.data); ++j) {
                    dst.data[j] = EC_READ_U8(domain_pd_ + src.data[j]);
                }
            }
            inputs.push_back(reinterpret_cast<const uint8_t*>(packet));
        } else {
            inputs.push_back(nullptr);
        }
    }

    return DecodeInputs(inputs, slave_count_);
}

void EthercatIGHHandle::Send(const MotorMessage& message) {
    if (!in_operational_.load()) {
        logger_->error("IGH EtherCAT not operational; dropping Send.");
        return;
    }

    QueueMessage(message);
}

void EthercatIGHHandle::Send(const MotorMessages& messages) {
    if (!in_operational_.load()) {
        logger_->error("IGH EtherCAT not operational; dropping Send.");
        return;
    }

    // 利用 base 层完成消息分组与帧打包（格式由 ConfigureSlaveConfigs 决定）。
    QueueMessages(messages);
}

void EthercatIGHHandle::SendSynchronized(const MotorMessages& messages) {
    if (!in_operational_.load()) {
        logger_->error("IGH EtherCAT not operational; dropping synchronized Send.");
        return;
    }

    QueueSynchronizedMessages(messages);
}

void EthercatIGHHandle::Loop(std::chrono::microseconds period) {
    // 主循环顺序：
    // receive -> process -> (可选写输出) -> queue -> Send -> decode回调
    auto next_wake = std::chrono::steady_clock::now();
    while (running_.load()) {
        ENCOS_TRACY_ZONE("EtherCAT::Cycle");
        next_wake += period;

        {
            ENCOS_TRACY_ZONE("EtherCAT::ReadProcessData");
            ecrt_master_receive(master_);
            ecrt_domain_process(domain_);
        }
        CheckDomainState();
        CheckMasterState();
        CheckSlaveStates();

        {
            ENCOS_TRACY_ZONE("EtherCAT::BuildFrame");
            OutputFrame frame;
            if (PrepareNextFrame(frame, slave_count_)) {
                WriteOutputs(frame);
            } else {
                ClearOutputs();
            }
        }

        {
            ENCOS_TRACY_ZONE("EtherCAT::WriteProcessData");
            ecrt_domain_queue(domain_);
            ecrt_master_send(master_);
        }

        {
            ENCOS_TRACY_ZONE("EtherCAT::MessageCallback");
            auto cb = CopyReceiveCallback();
            if (cb) {
                cb(ReadInputs());
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now < next_wake) {
            platform::SleepUntil(next_wake);
        }
        ENCOS_TRACY_FRAME("EtherCAT");
    }
}
