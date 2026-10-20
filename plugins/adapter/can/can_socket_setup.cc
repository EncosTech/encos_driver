// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "can_socket_setup.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/capability.h>
#include <memory>
#include <net/if.h>
#include <regex>
#include <string>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "can_socket_setup_internal.h"

namespace encos::can {

namespace {

constexpr double kSamplePointTolerance = 0.1;
constexpr int kCapNetAdmin = 12;
constexpr int kCapNetRaw = 13;

bool RunCommandWithOutput(const std::vector<std::string>& args, std::string* output,
                          std::string* error_detail) {
    if (args.empty()) {
        if (error_detail != nullptr) {
            *error_detail = "empty command";
        }
        errno = EINVAL;
        return false;
    }

    int pipe_fd[2] = {-1, -1};
    if (pipe(pipe_fd) != 0) {
        if (error_detail != nullptr) {
            *error_detail = "pipe failed";
        }
        return false;
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        if (error_detail != nullptr) {
            *error_detail = "fork failed";
        }
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return false;
    }

    if (pid == 0) {
        close(pipe_fd[0]);
        (void) dup2(pipe_fd[1], STDOUT_FILENO);
        (void) dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);
        /* File capabilities disappear across exec. Preserve only NET_ADMIN
         * for the fixed ip executable; never grant an executable from PATH. */
        if (geteuid() != 0) {
            struct __user_cap_header_struct header {
                _LINUX_CAPABILITY_VERSION_3, 0
            };
            struct __user_cap_data_struct caps[2]{};
            if (syscall(SYS_capget, &header, caps) < 0)
                _exit(126);
            constexpr uint32_t admin = 1U << CAP_NET_ADMIN;
            if (caps[0].permitted & admin) {
                caps[0].inheritable = admin;
                caps[1].inheritable = 0;
                if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) < 0)
                    _exit(126);
                if (syscall(SYS_capset, &header, caps) < 0 ||
                    prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_ADMIN, 0, 0) < 0)
                    _exit(126);
            }
        }
        execv("/usr/sbin/ip", argv.data());
        _exit(127);
    }

    close(pipe_fd[1]);
    std::string collected;
    char buffer[512];
    ssize_t n = 0;
    while ((n = read(pipe_fd[0], buffer, sizeof(buffer))) > 0) {
        collected.append(buffer, static_cast<std::size_t>(n));
    }
    close(pipe_fd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        if (error_detail != nullptr) {
            *error_detail = "waitpid failed";
        }
        return false;
    }

    if (output != nullptr) {
        *output = collected;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (error_detail != nullptr) {
            if (WIFEXITED(status)) {
                *error_detail = "command exit code=" + std::to_string(WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                *error_detail = "command killed by signal=" + std::to_string(WTERMSIG(status));
            } else {
                *error_detail = "command failed";
            }
            if (!collected.empty()) {
                *error_detail += ": " + collected;
            }
        }
        errno = EPERM;
        return false;
    }

    return true;
}

bool RunCommand(const std::vector<std::string>& args, std::string* error_detail) {
    std::string ignored_output;
    return RunCommandWithOutput(args, &ignored_output, error_detail);
}

bool GetEffectiveCapabilities(uint64_t* caps) {
    if (caps == nullptr) {
        errno = EINVAL;
        return false;
    }

    std::ifstream status_file("/proc/self/status");
    if (!status_file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(status_file, line)) {
        constexpr const char* kPrefix = "CapEff:\t";
        if (line.rfind(kPrefix, 0) == 0) {
            const std::string hex_value = line.substr(std::strlen(kPrefix));
            try {
                *caps = std::stoull(hex_value, nullptr, 16);
                return true;
            } catch (const std::exception&) {
                errno = EINVAL;
                return false;
            }
        }
    }

    errno = ENODATA;
    return false;
}

bool HasEffectiveCapability(int capability_bit) {
    uint64_t caps = 0;
    if (!GetEffectiveCapabilities(&caps)) {
        return false;
    }
    return (caps & (1ULL << capability_bit)) != 0;
}

}  // namespace

CanInterfaceConfig ParseCanDetails(const std::string& ip_details_output) {
    CanInterfaceConfig config{};

    const std::regex state_regex(R"(\bstate\s+(UP|DOWN)\b)");
    const std::regex bitrate_regex(R"(\bbitrate\s+([0-9]+)\b)");
    const std::regex sample_point_regex(R"(\bsample-point\s+([0-9.]+)\b)");
    const std::regex fd_regex(R"(\bfd\s+(on|off)\b)");
    const std::regex dbitrate_regex(R"(\bdbitrate\s+([0-9]+)\b)");
    const std::regex dsample_point_regex(R"(\bdsample-point\s+([0-9.]+)\b)");

    std::smatch match;
    if (std::regex_search(ip_details_output, match, state_regex) && match.size() >= 2) {
        config.up = match[1].str() == "UP";
    }

    if (!std::regex_search(ip_details_output, state_regex)) {
        const auto first_line = ip_details_output.substr(0, ip_details_output.find('\n'));
        config.up = std::regex_search(first_line, std::regex(R"(<[^>]*\bUP\b[^>]*>)"));
    }

    if (std::regex_search(ip_details_output, match, bitrate_regex) && match.size() >= 2) {
        try {
            config.bitrate = std::stoi(match[1].str());
        } catch (const std::exception&) {
            config.bitrate = 0;
        }
    }

    if (std::regex_search(ip_details_output, match, sample_point_regex) && match.size() >= 2) {
        try {
            config.sample_point = std::stod(match[1].str());
        } catch (const std::exception&) {
            config.sample_point = 0.0;
        }
    }

    if (std::regex_search(ip_details_output, match, fd_regex) && match.size() >= 2) {
        config.fd_on = match[1].str() == "on";
    }

    if (!std::regex_search(ip_details_output, fd_regex)) {
        config.fd_on =
            std::regex_search(ip_details_output, std::regex(R"(\bcan\s+<[^>]*\bFD\b[^>]*>)"));
    }

    if (std::regex_search(ip_details_output, match, dbitrate_regex) && match.size() >= 2) {
        try {
            config.dbitrate = std::stoi(match[1].str());
        } catch (const std::exception&) {
            config.dbitrate = 0;
        }
    }

    if (std::regex_search(ip_details_output, match, dsample_point_regex) && match.size() >= 2) {
        try {
            config.dsample_point = std::stod(match[1].str());
        } catch (const std::exception&) {
            config.dsample_point = 0.0;
        }
    }

    return config;
}

bool IsCanConfigMatchingTarget(const CanInterfaceConfig& config, const CanTiming& timing) {
    if (!config.up) {
        return false;
    }
    if (!config.fd_on) {
        return false;
    }
    if (config.bitrate != timing.bitrate) {
        return false;
    }
    if (std::fabs(config.sample_point - timing.sample_point) > kSamplePointTolerance) {
        return false;
    }
    if (config.dbitrate != timing.dbitrate) {
        return false;
    }
    if (std::fabs(config.dsample_point - timing.dsample_point) > kSamplePointTolerance) {
        return false;
    }
    return true;
}

std::vector<std::string> BuildCanSetupCommandArgs(const std::string& ifname,
                                                  const CanTiming& timing) {
    return {"ip",
            "link",
            "set",
            ifname,
            "type",
            "can",
            "bitrate",
            std::to_string(timing.bitrate),
            "sample-point",
            std::to_string(timing.sample_point),
            "fd",
            "on",
            "dbitrate",
            std::to_string(timing.dbitrate),
            "dsample-point",
            std::to_string(timing.dsample_point)};
}

std::vector<std::string> BuildCanUpCommandArgs(const std::string& ifname) {
    return {"ip", "link", "set", ifname, "up"};
}

std::vector<std::string> BuildCanDownCommandArgs(const std::string& ifname) {
    return {"ip", "link", "set", ifname, "down"};
}

namespace {

bool QueryCanDetails(const std::string& ifname, CanInterfaceConfig* config,
                     std::string* error_detail) {
    std::string output;
    if (!RunCommandWithOutput({"ip", "-details", "link", "show", ifname}, &output, error_detail)) {
        return false;
    }

    if (config != nullptr) {
        *config = ParseCanDetails(output);
    }
    return true;
}

}  // namespace

bool IsCanInterfaceInitialized(const std::string& ifname, const CanTiming& timing,
                               LoggerPtr logger) {
    CanInterfaceConfig config{};
    std::string detail;
    if (!QueryCanDetails(ifname, &config, &detail)) {
        logger->warn("Failed to inspect '{}' configuration: {}", ifname, detail);
        return false;
    }

    if (IsCanConfigMatchingTarget(config, timing)) {
        logger->info("CAN interface '{}' already initialized with target configuration.", ifname);
        return true;
    }

    logger->info(
        "CAN interface '{}' requires setup (up={}, fd_on={}, bitrate={}, sample_point={}, "
        "dbitrate={}, dsample_point={}).",
        ifname, config.up ? 1 : 0, config.fd_on ? 1 : 0, config.bitrate, config.sample_point,
        config.dbitrate, config.dsample_point);
    return false;
}

bool HasRequiredRuntimeCapabilities(LoggerPtr logger) {
    const bool has_net_raw = HasEffectiveCapability(kCapNetRaw);
    const bool has_net_admin = HasEffectiveCapability(kCapNetAdmin);
    if (!has_net_raw || !has_net_admin) {
        logger->error(
            "Missing effective capabilities: CAP_NET_RAW={}, CAP_NET_ADMIN={}. "
            "Set capabilities on the final executable before using static mode.",
            has_net_raw ? 1 : 0, has_net_admin ? 1 : 0);
        errno = EPERM;
        return false;
    }
    return true;
}

bool InitializeCanInterface(const std::string& ifname, const CanTiming& timing, LoggerPtr logger) {
    if (IsCanInterfaceInitialized(ifname, timing, logger)) {
        return true;
    }

    std::string detail;

    if (!RunCommand(BuildCanDownCommandArgs(ifname), &detail)) {
        logger->warn("Failed to set '{}' down before bitrate config: {}", ifname, detail);
    }

    if (!RunCommand(BuildCanSetupCommandArgs(ifname, timing), &detail)) {
        logger->error("Failed to set CAN bit timing on '{}': {}", ifname, detail);
        return false;
    }

    if (!RunCommand(BuildCanUpCommandArgs(ifname), &detail)) {
        logger->error("Failed to bring CAN interface '{}' up: {}", ifname, detail);
        return false;
    }

    if (!IsCanInterfaceInitialized(ifname, timing, logger)) {
        logger->error("CAN interface '{}' setup verification failed.", ifname);
        errno = EINVAL;
        return false;
    }

    logger->info("Initialized CAN interface '{}' with CAN FD-capable timing.", ifname);
    return true;
}

bool EnsureCanInterfaceReady(const std::string& ifname, const CanTiming& timing, LoggerPtr logger) {
    return HasRequiredRuntimeCapabilities(logger) && InitializeCanInterface(ifname, timing, logger);
}

int CreateConfiguredCanSocket(const std::string& ifname) {
    int can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can_fd < 0) {
        return -1;
    }

    struct ifreq ifr {};
    std::snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname.c_str());
    if (ioctl(can_fd, SIOCGIFINDEX, &ifr) < 0) {
        close(can_fd);
        return -1;
    }

    struct sockaddr_can addr {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(can_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(can_fd);
        return -1;
    }

    return can_fd;
}

namespace {

/** @brief 解析一个十进制字段并要求整串被消费 */
bool ParseFiniteDouble(const std::string& field, double* out) {
    if (field.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(field.c_str(), &end);
    if (end != field.c_str() + field.size() || errno == ERANGE || !std::isfinite(value)) {
        return false;
    }
    *out = value;
    return true;
}

}  // namespace

std::string FormatCanTiming(const CanTiming& timing) {
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%d,%.6f,%d,%.6f", timing.bitrate, timing.sample_point,
                  timing.dbitrate, timing.dsample_point);
    return std::string(buffer);
}

bool ParseCanTiming(const std::string& text, CanTiming* timing) {
    if (timing == nullptr) {
        return false;
    }

    double values[4] = {0.0, 0.0, 0.0, 0.0};
    std::size_t begin = 0;
    for (int i = 0; i < 4; ++i) {
        const std::size_t end = (i == 3) ? text.size() : text.find(',', begin);
        if (end == std::string::npos || end <= begin) {
            return false;
        }
        if (!ParseFiniteDouble(text.substr(begin, end - begin), &values[i])) {
            return false;
        }
        begin = end + 1;
        if (i == 3 && begin != text.size() + 1) {
            return false;
        }
    }

    const auto integral_in_range = [](double value) {
        return value > 0.0 && value <= static_cast<double>(std::numeric_limits<int>::max()) &&
               std::floor(value) == value;
    };
    const auto valid_sample_point = [](double value) {
        return value > 0.0 && value <= 1.0;
    };
    if (!integral_in_range(values[0]) || !valid_sample_point(values[1]) ||
        !integral_in_range(values[2]) || !valid_sample_point(values[3])) {
        return false;
    }

    CanTiming parsed;
    parsed.bitrate = static_cast<int>(values[0]);
    parsed.sample_point = values[1];
    parsed.dbitrate = static_cast<int>(values[2]);
    parsed.dsample_point = values[3];
    *timing = parsed;
    return true;
}

}  // namespace encos::can
