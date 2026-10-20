// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ethercat_handle.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

#include "platform/delay.h"
#include "utils/tracy.h"

namespace {
constexpr int kWkcErrorPeriod = 100;
constexpr int kWkcErrorMax = 20;
constexpr int kTimeoutMon = 500;
constexpr uint16 kWatchdogDividerRegister = 0x0400;
constexpr uint16 kWatchdogTimeRegister = 0x0420;
constexpr uint8 kSmWatchdogTrigger = 0x40;
constexpr auto kShutdownTimeout = std::chrono::milliseconds(500);
std::string HexWord(uint16 value) {
    char text[8];
    std::snprintf(text, sizeof(text), "0x%04x", static_cast<unsigned>(value));
    return text;
}
}  // namespace

EthercatHandle::EthercatHandle(std::string ifname, LoggerPtr logger, AdapterExtend extend)
    : EthercatBaseHandle(std::move(logger)),
      ifname_(std::move(ifname)),
      extend_(std::move(extend)) {
    try {
        ParseWatchdogOptions();
        if (!Initialize()) {
            throw std::runtime_error("Failed to Initialize EtherCAT");
        }

        running_.store(true);
    } catch (...) {
        running_.store(false);
        CloseContext();
        throw;
    }
}

EthercatHandle::~EthercatHandle() {
    Stop();
}

bool EthercatHandle::Initialize() {
    if (!OpenPort()) {
        return false;
    }
    context_closed_.store(false);

    if (ecx_config_init(&ctx_) <= 0) {
        logger_->error("No slaves found on {}.", ifname_);
        CloseContext();
        return false;
    }

    logger_->info("{} slaves found and configured.", ctx_.slavecount);
    if (ctx_.slavecount < 1) {
        logger_->error("No active slaves after configuration.");
        CloseContext();
        return false;
    }

    const auto io_map_capacity = ComputeEthercatIoMapUpperBound(ctx_, logger_);
    if (!io_map_capacity.has_value()) {
        CloseContext();
        return false;
    }
    io_map_.assign(*io_map_capacity, 0);

    for (int slave_idx = 1; slave_idx <= ctx_.slavecount; ++slave_idx) {
        ctx_.slavelist[slave_idx].CoEdetails &= ~ECT_COEDET_SDOCA;
    }

    ctx_.manualstatechange = 1;
    const int mapped_bytes = ecx_config_map_group(&ctx_, io_map_.data(), 0);
    if (!IsEthercatMappedSizeValid(mapped_bytes, io_map_.size())) {
        logger_->error("SOEM mapped {} bytes into a {} byte I/O map.", mapped_bytes,
                       io_map_.size());
        CloseContext();
        return false;
    }
    ecx_configdc(&ctx_);

    if (!ConfigureWatchdogs()) {
        CloseContext();
        return false;
    }
    ctx_.slavelist[0].state = EC_STATE_SAFE_OP;
    if (io_.write_state(&ctx_, 0) != ctx_.slavecount) {
        logger_->error("Failed to request SAFE_OP for all slaves.");
        CloseContext();
        return false;
    }
    for (int slave = 1; slave <= ctx_.slavecount; ++slave) {
        if (io_.state_check(&ctx_, slave, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4) !=
                EC_STATE_SAFE_OP ||
            ctx_.slavelist[slave].state != EC_STATE_SAFE_OP) {
            logger_->error("Slave {} failed to reach SAFE_OP: state={}, AL={}", slave,
                           HexWord(ctx_.slavelist[slave].state),
                           HexWord(ctx_.slavelist[slave].ALstatuscode));
            CloseContext();
            return false;
        }
    }
    ENCOS_LOG_DEBUG(logger_, "Mapped slaves and waiting for SAFE_OP.");

    slave_configs_.clear();
    slave_configs_.resize(static_cast<std::size_t>(ctx_.slavecount));
    for (int slave_idx = 1; slave_idx <= ctx_.slavecount; ++slave_idx) {
        auto& config = slave_configs_[static_cast<std::size_t>(slave_idx - 1)];
        const auto obytes = static_cast<std::size_t>(ctx_.slavelist[slave_idx].Obytes);
        config = ClassifyOutputPdoSize(obytes);
        if (HasSupportedPdo(config)) {
            ENCOS_LOG_DEBUG(logger_, "Slave {} configured with {} Buses from {} output bytes.",
                            slave_idx, config.bus_count, obytes);
        } else {
            logger_->warn("Slave {} has unsupported Obytes: {}", slave_idx, obytes);
        }
    }

    expected_wkc_.store((ctx_.grouplist[0].outputsWKC * 2) + ctx_.grouplist[0].inputsWKC);
    ENCOS_LOG_DEBUG(logger_, "Expected WKC {}.", expected_wkc_.load());

    /* CAN 位时序在 SAFE_OP 下发：邮箱已可用，输出过程数据尚未开始，因此改时序不会
       打断任何一侧的通信。 */
    if (!ApplyChannelTimings()) {
        CloseContext();
        return false;
    }

    return TransitionToOperational();
}

bool EthercatHandle::WriteSdo(int slave, std::uint16_t index, std::uint8_t subindex,
                              std::uint32_t value, std::uint8_t size) {
    const int result = io_.sdo_write(&ctx_, static_cast<uint16_t>(slave), index, subindex, FALSE,
                                     static_cast<int>(size), &value, EC_TIMEOUTRXM);
    if (result == 0) {
        /* Logger::Format 只替换裸 {}，不支持格式说明符，且 uint8_t 会按字符输出；
           因此把对象索引预格式化成十六进制文本，并把子索引提升为无符号整数。 */
        char index_text[8];
        std::snprintf(index_text, sizeof(index_text), "0x%04x", static_cast<unsigned>(index));
        logger_->error("CAN timing SDO write rejected: slave {} index {} subindex {}", slave,
                       index_text, static_cast<unsigned>(subindex));
        return false;
    }
    return true;
}

bool EthercatHandle::ReadSdoU32(int slave, std::uint16_t index, std::uint8_t subindex,
                                std::uint32_t* out) {
    int size = static_cast<int>(sizeof(*out));
    *out = 0;
    if (io_.sdo_read(&ctx_, static_cast<uint16_t>(slave), index, subindex, FALSE, &size, out,
                     EC_TIMEOUTRXM) == 0) {
        return false;
    }
    return size == static_cast<int>(sizeof(*out));
}

bool EthercatHandle::ApplyBusTiming(int slave, unsigned channel,
                                    const nlohmann::json& override_json) {
    const std::uint16_t index =
        static_cast<std::uint16_t>(encos::ethercat::kCanTimingSdoBase + channel);

    // 先做一次纯字段校验：未知字段、类型或取值范围非法在这里就抛，不产生任何邮箱流量。
    {
        encos::ethercat::CanTiming probe;
        encos::ethercat::ApplyGatewayCanTiming(override_json, &probe);
    }

    // 固件先把子索引 1..4 暂存在寄存器里、由 apply
    // 提交，所以条目里的部分字段必须合并到
    // 「当前暂存值」之上，而不是另起一份默认值。
    encos::ethercat::CanTiming timing;
    std::uint32_t per_mille = 0;
    if (!ReadSdoU32(slave, index, encos::ethercat::kCanTimingSubNominalBps, &timing.nominal_bps) ||
        !ReadSdoU32(slave, index, encos::ethercat::kCanTimingSubDataBps, &timing.data_bps) ||
        !ReadSdoU32(slave, index, encos::ethercat::kCanTimingSubNominalSamplePoint, &per_mille)) {
        logger_->error("Slave {} CAN{}: reading the staged timing back failed", slave, channel);
        return false;
    }
    timing.nominal_sample_point = static_cast<double>(per_mille) / 1000.0;
    if (!ReadSdoU32(slave, index, encos::ethercat::kCanTimingSubDataSamplePoint, &per_mille)) {
        logger_->error("Slave {} CAN{}: reading the staged timing back failed", slave, channel);
        return false;
    }
    timing.data_sample_point = static_cast<double>(per_mille) / 1000.0;

    encos::ethercat::ApplyGatewayCanTiming(override_json, &timing);

    const std::uint32_t nominal_sp = encos::ethercat::ToPerMille(timing.nominal_sample_point);
    const std::uint32_t data_sp = encos::ethercat::ToPerMille(timing.data_sample_point);
    if (!WriteSdo(slave, index, encos::ethercat::kCanTimingSubNominalBps, timing.nominal_bps, 4) ||
        !WriteSdo(slave, index, encos::ethercat::kCanTimingSubDataBps, timing.data_bps, 4) ||
        !WriteSdo(slave, index, encos::ethercat::kCanTimingSubNominalSamplePoint, nominal_sp, 4) ||
        !WriteSdo(slave, index, encos::ethercat::kCanTimingSubDataSamplePoint, data_sp, 4) ||
        !WriteSdo(slave, index, encos::ethercat::kCanTimingSubApply, 1, 1)) {
        std::uint32_t status = 0;
        if (ReadSdoU32(slave, index, encos::ethercat::kCanTimingSubStatus, &status)) {
            logger_->error("Slave {} CAN{} timing was not applied (status {})", slave, channel,
                           status);
        }
        return false;
    }
    logger_->info("Slave {} CAN{} timing updated: {}/{} bps, {}/{} per-mille", slave, channel,
                  timing.nominal_bps, timing.data_bps, nominal_sp, data_sp);
    return true;
}

bool EthercatHandle::ReconfigureBuses(const AdapterExtend& extend) {
    if (!running_.load() || ctx_.slavecount <= 0) {
        throw std::runtime_error("Ethercat adapter is no longer running; bus timing not updated");
    }
    for (const auto& bus : extend.DeclaredBuses()) {
        if (bus.first < 0 || bus.first >= ctx_.slavecount) {
            throw std::runtime_error("Ethercat adapter has no slave '" + std::to_string(bus.first) +
                                     "'");
        }
        if (bus.second < 0 || static_cast<std::size_t>(bus.first) >= slave_configs_.size() ||
            static_cast<std::size_t>(bus.second) >= slave_configs_[bus.first].bus_count ||
            bus.second >= static_cast<int>(encos::ethercat::kGatewayChannelCount)) {
            throw std::runtime_error("Ethercat slave " + std::to_string(bus.first) +
                                     " has no CAN channel '" + std::to_string(bus.second) + "'");
        }
    }

    ReconfigureRequest request{extend, {}};
    auto completion = request.completion.get_future();
    {
        std::lock_guard<std::mutex> lock(reconfigure_mutex_);
        if (!running_.load()) {
            throw std::runtime_error("EtherCAT handle stopped; bus timing update aborted");
        }
        pending_reconfigure_.push_back(std::move(request));
    }

    // 超时只终止当前调用的等待；迟到的完成结果仍属于原请求。
    if (completion.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
        throw std::runtime_error("Ethercat bus timing update was not serviced by the loop");
    }
    completion.get();
    return true;
}

void EthercatHandle::ServicePendingReconfigure() {
    ReconfigureRequest request;
    {
        std::lock_guard<std::mutex> lock(reconfigure_mutex_);
        if (pending_reconfigure_.empty()) {
            return;
        }
        request = std::move(pending_reconfigure_.front());
        pending_reconfigure_.pop_front();
    }

    try {
        for (const auto& bus : request.extend.DeclaredBuses()) {
            const auto* override_json = request.extend.BusOverride(bus.first, bus.second);
            if (override_json == nullptr) {
                continue;
            }
            if (!ApplyBusTiming(bus.first + 1, static_cast<unsigned>(bus.second), *override_json)) {
                throw std::runtime_error("CAN timing update was rejected by the slave");
            }
        }
        request.completion.set_value();
    } catch (...) {
        request.completion.set_exception(std::current_exception());
    }
}

void EthercatHandle::FailPendingReconfigure(const char* reason) {
    std::lock_guard<std::mutex> lock(reconfigure_mutex_);
    for (auto& request : pending_reconfigure_) {
        request.completion.set_exception(std::make_exception_ptr(std::runtime_error(reason)));
    }
    pending_reconfigure_.clear();
}

bool EthercatHandle::ApplyChannelTimings() {
    /* 信封里显式点名的总线必须真实存在，否则是配置错误 */
    for (const auto& bus : extend_.DeclaredBuses()) {
        if (bus.first < 0 || bus.first >= ctx_.slavecount) {
            logger_->error("Ethercat adapter has no slave '{}' ({} present)", bus.first,
                           ctx_.slavecount);
            return false;
        }
        if (bus.second < 0 || static_cast<std::size_t>(bus.first) >= slave_configs_.size() ||
            static_cast<std::size_t>(bus.second) >= slave_configs_[bus.first].bus_count ||
            bus.second >= static_cast<int>(encos::ethercat::kGatewayChannelCount)) {
            logger_->error("Ethercat slave {} has no CAN channel '{}'", bus.first, bus.second);
            return false;
        }
    }
    std::vector<encos::ethercat::ChannelTimingPlan> plans;
    for (int idx = 0; idx < ctx_.slavecount; ++idx) {
        const auto count = static_cast<std::size_t>(idx) < slave_configs_.size()
                               ? slave_configs_[idx].bus_count
                               : 0;
        plans.push_back(encos::ethercat::BuildChannelTimingPlan(extend_, idx, count));
    }
    for (int idx = 0; idx < ctx_.slavecount; ++idx) {
        const int slave = idx + 1; /* SOEM 从站索引从 1 起 */
        const auto& plan = plans[idx];
        for (unsigned channel = 0; channel < plan.size(); ++channel) {
            if (!plan[channel].has_value()) {
                continue;
            }
            const auto& timing = *plan[channel];
            const std::uint16_t index =
                static_cast<std::uint16_t>(encos::ethercat::kCanTimingSdoBase + channel);
            const std::uint32_t nominal_sp =
                encos::ethercat::ToPerMille(timing.nominal_sample_point);
            const std::uint32_t data_sp = encos::ethercat::ToPerMille(timing.data_sample_point);
            if (!WriteSdo(slave, index, encos::ethercat::kCanTimingSubNominalBps,
                          timing.nominal_bps, 4) ||
                !WriteSdo(slave, index, encos::ethercat::kCanTimingSubDataBps, timing.data_bps,
                          4) ||
                !WriteSdo(slave, index, encos::ethercat::kCanTimingSubNominalSamplePoint,
                          nominal_sp, 4) ||
                !WriteSdo(slave, index, encos::ethercat::kCanTimingSubDataSamplePoint, data_sp,
                          4) ||
                !WriteSdo(slave, index, encos::ethercat::kCanTimingSubApply, 1, 1)) {
                std::uint32_t status = 0;
                if (ReadSdoU32(slave, index, encos::ethercat::kCanTimingSubStatus, &status)) {
                    logger_->error("Slave {} CAN{} timing was not applied (status {})", slave,
                                   channel, status);
                }
                return false;
            }
            logger_->info("Slave {} CAN{} timing applied: {}/{} bps, {}/{} per-mille", slave,
                          channel, timing.nominal_bps, timing.data_bps, nominal_sp, data_sp);
        }
    }
    return true;
}

bool EthercatHandle::TransitionToOperational() {
    operational_requested_ = true;
    ctx_.slavelist[0].state = EC_STATE_OPERATIONAL;
    ecx_send_processdata(&ctx_);
    ecx_receive_processdata(&ctx_, EC_TIMEOUTRET);
    ecx_writestate(&ctx_, 0);

    int retries = 40;
    do {
        ecx_send_processdata(&ctx_);
        wkc_.store(ecx_receive_processdata(&ctx_, EC_TIMEOUTRET));
        ecx_statecheck(&ctx_, 0, EC_STATE_OPERATIONAL, 50000);
    } while (retries-- && (ctx_.slavelist[0].state != EC_STATE_OPERATIONAL));

    if (ctx_.slavelist[0].state == EC_STATE_OPERATIONAL) {
        ENCOS_LOG_DEBUG(logger_, "Operational state reached for all slaves.");
        // 状态切换完成后仍需由运行循环确认一帧有效 PDO，避免上层初始化请求
        // 在启动瞬间进入尚未稳定的 SAFE_OP 链路。
        in_operational_.store(false);
        slaves_operational_ = true;
        return true;
    }

    logger_->error("Not all slaves reached operational state.");
    ecx_readstate(&ctx_);
    for (int i = 1; i <= ctx_.slavecount; ++i) {
        if (ctx_.slavelist[i].state != EC_STATE_OPERATIONAL) {
            logger_->error("Slave {} State=0x{:#x} StatusCode=0x{:#x} : {}", i,
                           static_cast<int>(ctx_.slavelist[i].state),
                           ctx_.slavelist[i].ALstatuscode,
                           ec_ALstatuscode2string(ctx_.slavelist[i].ALstatuscode));
        }
    }
    return false;
}

void EthercatHandle::DegradedHandler() {
    SetSendQueueWarningsSuppressed(true);
    if (in_operational_.exchange(false)) {
        logger_->warn(
            "EtherCAT link degraded; continuing PDO exchange and "
            "attempting recovery.");
    }
    slaves_operational_ = false;
}

void EthercatHandle::LogBadWkc() {
    logger_->error("Dropped packet (Bad WKC: {})", wkc_.load());
}

void EthercatHandle::RequestStop() {
    running_.store(false);
    in_operational_.store(false);
    FailPendingReconfigure("EtherCAT handle stopped; bus timing update aborted");
}

bool EthercatHandle::Ok() const {
    return running_.load() && in_operational_.load();
}

bool EthercatHandle::WaitUntilOperational(std::chrono::milliseconds timeout) const {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (running_.load() && !Ok()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        platform::SleepFor(std::chrono::milliseconds(1));
    }
    return Ok();
}

void EthercatHandle::ParseWatchdogOptions() {
    auto common = extend_.Common();
    const auto it = common.find("watchdog_timeout_ms");
    if (it != common.end()) {
        if ((!it->is_number_integer() && !it->is_number_unsigned()) || *it < 1 || *it > 60000) {
            throw std::runtime_error(
                "Ethercat watchdog_timeout_ms must be an integer in [1, 60000]");
        }
        watchdog_timeout_ms_ = it->get<unsigned>();
        common.erase(it);
    }
    for (const auto& bus : extend_.DeclaredBuses()) {
        const auto& overrides = *extend_.BusOverride(bus.first, bus.second);
        if (overrides.contains("watchdog_timeout_ms")) {
            throw std::runtime_error("Ethercat watchdog_timeout_ms is an adapter-level option");
        }
        common["buses"][std::to_string(bus.first) + ":" + std::to_string(bus.second)] = overrides;
    }
    extend_ = AdapterExtend::Parse(common.dump());
}

bool EthercatHandle::ConfigureWatchdogs(int only_slave) {
    const int first = only_slave == 0 ? 1 : only_slave;
    const int last = only_slave == 0 ? ctx_.slavecount : only_slave;
    for (int slave = first; slave <= last; ++slave) {
        auto& info = ctx_.slavelist[slave];
        if (info.Obits == 0 && info.Obytes == 0) {
            continue;
        }
        const auto read = [&](uint16 address, auto& value) {
            if (io_.read_register(&ctx_.port, info.configadr, address, sizeof(value), &value,
                                  EC_TIMEOUTRET3) == 1) {
                return true;
            }
            logger_->error("Slave {} watchdog register {}: read failed", slave, HexWord(address));
            return false;
        };
        const auto write = [&](uint16 address, auto value) {
            if (io_.write_register(&ctx_.port, info.configadr, address, sizeof(value), &value,
                                   EC_TIMEOUTRET3) != 1) {
                logger_->error("Slave {} watchdog register {}: write failed", slave,
                               HexWord(address));
                return false;
            }
            auto actual = value;
            if (!read(address, actual)) {
                return false;
            }
            if (actual != value) {
                logger_->error("Slave {} watchdog register {}: readback mismatch", slave,
                               HexWord(address));
                return false;
            }
            return true;
        };
        uint16 divider = 0;
        if (!read(kWatchdogDividerRegister, divider)) {
            return false;
        }
        const uint64_t tick = static_cast<uint64_t>(etohs(divider)) + 2;
        const uint64_t count =
            (static_cast<uint64_t>(watchdog_timeout_ms_) * 25000 + tick - 1) / tick;
        if (count == 0 || count > 0xffff) {
            logger_->error(
                "Slave {} watchdog timeout {} ms cannot be represented "
                "with divider {}",
                slave, watchdog_timeout_ms_, etohs(divider));
            return false;
        }
        if (!write(kWatchdogTimeRegister, static_cast<uint16>(htoes(static_cast<uint16>(count))))) {
            return false;
        }
        bool has_output_sm = false;
        for (unsigned sm = 0; sm < EC_MAXSM; ++sm) {
            auto& manager = info.SM[sm];
            if (info.SMtype[sm] != 3 || manager.SMlength == 0 || manager.StartAddr == 0) {
                continue;
            }
            has_output_sm = true;
            const auto base = static_cast<uint16>(ECT_REG_SM0 + sm * sizeof(ec_smt));
            uint8 control = 0;
            uint8 activation = 0;
            if (!read(static_cast<uint16>(base + 4), control) ||
                !read(static_cast<uint16>(base + 6), activation)) {
                return false;
            }
            const auto enabled_control = static_cast<uint8>(control | kSmWatchdogTrigger);
            if ((control & kSmWatchdogTrigger) == 0) {
                if (!write(static_cast<uint16>(base + 6), static_cast<uint8>(activation & ~1U)) ||
                    !write(static_cast<uint16>(base + 4), enabled_control) ||
                    !write(static_cast<uint16>(base + 6), activation)) {
                    return false;
                }
            }
            manager.SMflags = htoel(etohl(manager.SMflags) | kSmWatchdogTrigger);
        }
        if (!has_output_sm) {
            logger_->error("Slave {} has outputs but no mapped output SyncManager", slave);
            return false;
        }
        ctx_.userdata = this;
        info.PO2SOconfig = RestoreWatchdog;
        logger_->info(
            "Slave {} process-data watchdog configured: requested {} ms, "
            "divider {}, count {}",
            slave, watchdog_timeout_ms_, etohs(divider), count);
    }
    return true;
}

int EthercatHandle::RestoreWatchdog(ecx_contextt* context, uint16 slave) {
    auto& handle = *static_cast<EthercatHandle*>(context->userdata);
    if (handle.ConfigureWatchdogs(slave)) {
        return 1;
    }
    handle.logger_->error("Slave {} watchdog restore failed; stopping EtherCAT recovery", slave);
    handle.RequestStop();
    return 0;
}

void EthercatHandle::LeaveOperational() {
    operational_requested_ = false;
    if (ctx_.slavecount <= 0) {
        return;
    }
    ctx_.slavelist[0].state = EC_STATE_SAFE_OP;
    if (io_.write_state(&ctx_, 0) != ctx_.slavecount) {
        logger_->warn("Not all slaves acknowledged the shutdown SAFE_OP request.");
    }
    const auto deadline = std::chrono::steady_clock::now() + kShutdownTimeout;
    for (int slave = 1; slave <= ctx_.slavecount; ++slave) {
        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        if (remaining <= 0) {
            logger_->warn("Shutdown state confirmation timed out before slave {}.", slave);
            break;
        }
        const auto state =
            io_.state_check(&ctx_, slave, EC_STATE_SAFE_OP, static_cast<int>(remaining));
        if (state != EC_STATE_SAFE_OP) {
            logger_->warn(
                "Slave {} did not confirm SAFE_OP on shutdown: "
                "state={}, AL={}",
                slave, HexWord(ctx_.slavelist[slave].state),
                HexWord(ctx_.slavelist[slave].ALstatuscode));
        }
    }
}

void EthercatHandle::CloseContext() {
    if (!context_closed_.exchange(true)) {
        if (operational_requested_) {
            LeaveOperational();
        }
        io_.close(&ctx_);
    }
}

void EthercatHandle::Stop() {
    RequestStop();
    CloseContext();
}

void EthercatHandle::CheckState() {
    if (!running_.load()) {
        return;
    }
    const int state_result = io_.read_state(&ctx_);
    bool all_operational = state_result > EC_STATE_NONE && ctx_.slavecount > 0;
    if (!all_operational && in_operational_.load()) {
        DegradedHandler();
    }
    for (int slave = 1; slave <= ctx_.slavecount && running_.load(); ++slave) {
        auto& device = ctx_.slavelist[slave];
        if (device.group != current_group_) {
            continue;
        }
        if (device.state == EC_STATE_OPERATIONAL) {
            device.islost = false;
            continue;
        }
        all_operational = false;
        if (in_operational_.load()) {
            DegradedHandler();
        }
        if (device.state == (EC_STATE_SAFE_OP + EC_STATE_ERROR)) {
            logger_->error("Slave {} SAFE_OP + ERROR, acking.", slave);
            device.state = EC_STATE_SAFE_OP + EC_STATE_ACK;
            io_.write_state(&ctx_, slave);
        } else if (device.state == EC_STATE_SAFE_OP) {
            logger_->error("Slave {} SAFE_OP, requesting OPERATIONAL.", slave);
            device.state = EC_STATE_OPERATIONAL;
            io_.write_state(&ctx_, slave);
        } else if (device.state > EC_STATE_NONE) {
            if (io_.reconfigure(&ctx_, slave, kTimeoutMon) >= EC_STATE_PRE_OP) {
                device.islost = false;
                logger_->info("Slave {} reconfigured.", slave);
            }
        } else {
            io_.state_check(&ctx_, slave, EC_STATE_OPERATIONAL, EC_TIMEOUTRET);
            if (device.state == EC_STATE_NONE) {
                device.islost = true;
                logger_->error("Slave {} lost.", slave);
                if (io_.recover(&ctx_, slave, kTimeoutMon)) {
                    device.islost = false;
                    logger_->info("Slave {} recovered.", slave);
                }
            }
        }
    }
    // 写入 OP 请求并不表示已进入 OP；必须等待下一次实际状态读取确认。
    const bool was_all_operational = slaves_operational_;
    slaves_operational_ = all_operational;
    ctx_.grouplist[current_group_].docheckstate = !all_operational;
    if (all_operational && !was_all_operational) {
        logger_->info("All slaves resumed OPERATIONAL.");
    }
}

void EthercatHandle::WriteOutputs(const OutputFrame& packets) {
    const auto count = static_cast<std::size_t>(ctx_.slavecount);
    for (std::size_t slave = 0; slave < count; ++slave) {
        auto* dest = reinterpret_cast<uint8_t*>(ctx_.slavelist[slave + 1].outputs);
        if (dest == nullptr) {
            continue;
        }

        const std::size_t obytes = static_cast<std::size_t>(ctx_.slavelist[slave + 1].Obytes);
        if (slave < packets.size() && !packets[slave].empty()) {
            const auto& buffer = packets[slave];
            const std::size_t bytes = std::min(buffer.size(), obytes);
            std::memcpy(dest, buffer.data(), bytes);
            if (bytes < obytes) {
                std::memset(dest + bytes, 0, obytes - bytes);
            }
            continue;
        }

        std::memset(dest, 0, obytes);
    }
}

MotorMessages EthercatHandle::ReadInputs() {
    const auto count = static_cast<std::size_t>(ctx_.slavecount);
    std::vector<const uint8_t*> inputs;
    inputs.reserve(count);
    for (std::size_t slave = 0; slave < count; ++slave) {
        inputs.push_back(reinterpret_cast<const uint8_t*>(ctx_.slavelist[slave + 1].inputs));
    }
    return DecodeInputs(inputs, count);
}

void EthercatHandle::Send(const MotorMessage& message) {
    if (!running_.load()) {
        return;
    }

    if (ctx_.slavecount <= 0) {
        logger_->error("No slaves available for Send.");
        return;
    }

    QueueMessage(message);
}

void EthercatHandle::Send(const MotorMessages& messages) {
    if (!running_.load()) {
        return;
    }

    if (ctx_.slavecount <= 0) {
        logger_->error("No slaves available for Send.");
        return;
    }

    QueueMessages(messages);
}

void EthercatHandle::SendSynchronized(const MotorMessages& messages) {
    if (!running_.load()) {
        return;
    }

    if (ctx_.slavecount <= 0) {
        logger_->error("No slaves available for synchronized Send.");
        return;
    }

    QueueSynchronizedMessages(messages);
}

void EthercatHandle::ExchangeOnce() {
    if (!running_.load()) {
        return;
    }
    const bool was_operational = in_operational_.load();
    OutputFrame frame;
    if (was_operational) {
        PrepareNextFrame(frame, static_cast<std::size_t>(ctx_.slavecount));
    }
    WriteOutputs(frame);
    io_.send(&ctx_);
    wkc_.store(io_.receive(&ctx_, EC_TIMEOUTRET));
    const bool bad_wkc = wkc_.load() < expected_wkc_.load() || expected_wkc_.load() <= 0;
    if (wkc_error_iteration_.load() >= kWkcErrorPeriod) {
        wkc_error_iteration_.store(0);
        wkc_error_count_.store(0);
    }
    if (bad_wkc) {
        if (!was_operational) {
            // 恢复阶段的坏 WKC 不属于正常 OP 链路错误，不能消耗降级阈值。
            slaves_operational_ = false;
            return;
        }
        wkc_error_count_.fetch_add(1);
        LogBadWkc();
    }
    wkc_error_iteration_.fetch_add(1);
    if (bad_wkc) {
        const bool reached_wkc_threshold = wkc_error_count_.load() >= kWkcErrorMax;
        if (was_operational && reached_wkc_threshold)
            logger_->error(
                "EtherCAT WKC error count exceeded threshold; entering "
                "degraded mode.");
        if (reached_wkc_threshold)
            DegradedHandler();
        return;
    }
    if (!was_operational) {
        if (running_.load() && slaves_operational_) {
            SetSendQueueWarningsSuppressed(false);
            in_operational_.store(true);
            wkc_error_count_.store(0);
            wkc_error_iteration_.store(0);
            logger_->warn("EtherCAT recovered: all slaves OPERATIONAL with valid WKC.");
        }
    }
    if (Ok()) {
        ReceiveCallback callback = CopyReceiveCallback();
        if (callback) {
            callback(ReadInputs());
        }
    }
}

void EthercatHandle::Loop(std::chrono::microseconds period) {
    auto next_wake = std::chrono::steady_clock::now();
    auto last_overrun_warn = std::chrono::steady_clock::now();
    bool has_warned_overrun = false;
    auto next_state_check = next_wake;
    bool first_cycle = true;

    while (running_.load()) {
        ENCOS_TRACY_ZONE("EtherCAT::Cycle");
        next_wake += period;
        const bool wkc_invalid = wkc_.load() < expected_wkc_.load() || expected_wkc_.load() <= 0;

        if ((first_cycle || !in_operational_.load() ||
             ctx_.grouplist[current_group_].docheckstate || wkc_invalid) &&
            std::chrono::steady_clock::now() >= next_state_check) {
            CheckState();
            next_state_check = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
        }
        ExchangeOnce();
        ServicePendingReconfigure();
        first_cycle = false;
        if (wkc_.load() < expected_wkc_.load() || expected_wkc_.load() <= 0) {
            next_state_check = std::chrono::steady_clock::now();
        }

        const auto now = std::chrono::steady_clock::now();
        if (now <= next_wake) {
            platform::SleepUntil(next_wake);
        } else if (!has_warned_overrun || now - last_overrun_warn >= std::chrono::seconds(1)) {
            last_overrun_warn = now;
            has_warned_overrun = true;
            logger_->warn(
                "Loop overrun by {} us",
                std::chrono::duration_cast<std::chrono::microseconds>(now - next_wake).count());
        }
        ENCOS_TRACY_FRAME("EtherCAT");
    }
}
