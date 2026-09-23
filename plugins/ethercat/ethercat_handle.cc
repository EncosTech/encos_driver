// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ethercat_handle.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "platform/delay.h"
#include "utils/tracy.h"

namespace {
constexpr int kWkcErrorPeriod = 100;
constexpr int kWkcErrorMax = 20;
constexpr int kTimeoutMon = 500;
}  // namespace

EthercatHandle::EthercatHandle(std::string ifname, LoggerPtr logger)
    : EthercatBaseHandle(std::move(logger)), ifname_(std::move(ifname)) {
    try {
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

    const int mapped_bytes = ecx_config_map_group(&ctx_, io_map_.data(), 0);
    if (!IsEthercatMappedSizeValid(mapped_bytes, io_map_.size())) {
        logger_->error("SOEM mapped {} bytes into a {} byte I/O map.", mapped_bytes,
                       io_map_.size());
        CloseContext();
        return false;
    }
    ecx_configdc(&ctx_);

    ecx_statecheck(&ctx_, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);
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

    return TransitionToOperational();
}

bool EthercatHandle::TransitionToOperational() {
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
        logger_->warn("EtherCAT link degraded; continuing PDO exchange and attempting recovery.");
    }
    slaves_operational_ = false;
}

void EthercatHandle::LogBadWkc() {
    logger_->error("Dropped packet (Bad WKC: {})", wkc_.load());
}

void EthercatHandle::RequestStop() {
    running_.store(false);
    in_operational_.store(false);
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

void EthercatHandle::CloseContext() {
    if (!context_closed_.exchange(true)) {
        ecx_close(&ctx_);
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
            logger_->error("EtherCAT WKC error count exceeded threshold; entering degraded mode.");
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
