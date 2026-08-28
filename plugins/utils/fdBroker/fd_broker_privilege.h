#pragma once

#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "platform/log.h"

namespace encos {
namespace fd_broker {

constexpr const char* kMsgEscalateRestarting = "ESCALATE_RESTARTING";
constexpr const char* kMsgPkexecUnavailable = "PKEXEC_UNAVAILABLE";
constexpr const char* kMsgPkexecLaunchFailed = "PKEXEC_LAUNCH_FAILED";
constexpr const char* kMsgAlreadyEscalated = "ESCALATION_ALREADY_ATTEMPTED";

struct PrivilegeBootstrapResult {
    bool has_capabilities = false;
    bool started_escalation = false;
    int error_number = 0;
    int32_t escalation_pid = 0;
    std::string tag;
    std::string detail;
    std::string manual_command;
    std::string script_path;
};

inline std::vector<std::string> BuildBrokerRestartArguments(int argc, char* const argv[]) {
    if (argc < 8 || argv == nullptr) {
        return {};
    }
    return std::vector<std::string>(argv + 1, argv + 8);
}

#if defined(__linux__)
namespace detail {

inline std::string trim(const std::string& s) {
    std::size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin])) != 0) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return s.substr(begin, end - begin);
}

inline std::string ShellQuote(const std::string& v) {
    std::string out;
    out.reserve(v.size() + 2);
    out.push_back('\'');
    for (char c : v) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

inline std::string JoinShellCommand(const std::vector<std::string>& args) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            oss << ' ';
        }
        oss << ShellQuote(args[i]);
    }
    return oss.str();
}

inline std::string FormatManualSetcapCommand(const std::string& executable_path) {
    return "sudo setcap cap_net_raw,cap_net_admin+ep " + ShellQuote(executable_path);
}

inline bool HasCapabilityString(const std::string& caps, const char* needle) {
    return caps.find(needle) != std::string::npos;
}

inline bool HasRequiredCapabilitiesByGetcap(const std::string& executable_path, LoggerPtr logger) {
    const std::string cmd = "getcap " + ShellQuote(executable_path) + " 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (fp == nullptr) {
        logger->warn("Failed to execute getcap for '{}': errno={}", executable_path, errno);
        return false;
    }

    std::array<char, 512> buffer{};
    std::string output;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), fp) != nullptr) {
        output += buffer.data();
    }
    const int rc = pclose(fp);
    if (rc != 0) {
        return false;
    }

    output = trim(output);
    if (output.empty()) {
        return false;
    }
    return HasCapabilityString(output, "cap_net_raw") &&
           HasCapabilityString(output, "cap_net_admin");
}

inline bool IsCommandAvailable(const char* cmd) {
    const char* path = std::getenv("PATH");
    if (path == nullptr || *path == '\0') {
        return false;
    }
    std::string path_copy(path);
    std::size_t start = 0;
    while (start <= path_copy.size()) {
        const std::size_t pos = path_copy.find(':', start);
        const std::string dir =
            path_copy.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        const std::string full = dir.empty() ? std::string(cmd) : dir + "/" + cmd;
        if (access(full.c_str(), X_OK) == 0) {
            return true;
        }
        if (pos == std::string::npos) {
            break;
        }
        start = pos + 1;
    }
    return false;
}

inline std::string GetCurrentExecutablePath() {
    std::array<char, 4096> path{};
    const ssize_t n = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (n <= 0) {
        return std::string();
    }
    path[static_cast<std::size_t>(n)] = '\0';
    return std::string(path.data());
}

inline bool WriteAll(int fd, const std::string& data) {
    const char* p = data.c_str();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const ssize_t n = write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        p += n;
        remaining -= static_cast<std::size_t>(n);
    }
    return true;
}

inline bool CreatePrivilegeScript(const std::string& script_path_template,
                                  const std::string& executable_path, uid_t run_uid, gid_t run_gid,
                                  const std::vector<std::string>& broker_args,
                                  std::string* script_path_out, LoggerPtr logger) {
    std::vector<char> tmp(script_path_template.begin(), script_path_template.end());
    tmp.push_back('\0');
    const int fd = mkstemp(tmp.data());
    if (fd < 0) {
        logger->error("Failed to Create temporary privilege script: errno={}", errno);
        return false;
    }

    const std::string script_path(tmp.data());
    if (fchmod(fd, S_IRWXU) != 0) {
        logger->error("Failed to chmod temporary script '{}': errno={}", script_path, errno);
        close(fd);
        unlink(script_path.c_str());
        return false;
    }

    std::vector<std::string> rerun_command;
    rerun_command.emplace_back(executable_path);
    rerun_command.insert(rerun_command.end(), broker_args.begin(), broker_args.end());
    const std::string rerun_line = JoinShellCommand(rerun_command);

    std::ostringstream script;
    script << "#!/bin/sh\n";
    script << "set -eu\n";
    script << "trap 'rm -f -- \"$0\"' EXIT\n";
    script << "setcap cap_net_raw,cap_net_admin+ep " << ShellQuote(executable_path) << "\n";
    script << "if command -v setpriv >/dev/null 2>&1; then\n";
    script << "  exec env ENCOS_FDBROKER_ESCALATED=1 setpriv --reuid " << run_uid << " --regid "
           << run_gid << " --clear-groups " << rerun_line << "\n";
    script << "fi\n";
    script << "echo 'setpriv not available; please run manually:' >&2\n";
    script << "echo " << ShellQuote(FormatManualSetcapCommand(executable_path)) << " >&2\n";
    script << "exit 127\n";

    const std::string content = script.str();
    const bool ok = WriteAll(fd, content);
    if (!ok) {
        logger->error("Failed to write temporary script '{}': errno={}", script_path, errno);
        close(fd);
        unlink(script_path.c_str());
        return false;
    }
    close(fd);
    *script_path_out = script_path;
    return true;
}

[[noreturn]] inline void ReplaceBrokerWithPkexecScript(const std::string& script_path) {
    execlp("pkexec", "pkexec", "/bin/sh", script_path.c_str(), static_cast<char*>(nullptr));
    _exit(127);
}

}  // namespace detail
#endif

inline PrivilegeBootstrapResult EnsureBrokerCapabilitiesOrEscalate(
    const std::string& executable_path, const std::vector<std::string>& broker_args,
    LoggerPtr logger) {
    PrivilegeBootstrapResult result;
#if !defined(__linux__)
    (void) executable_path;
    (void) broker_args;
    (void) logger;
    result.has_capabilities = true;
    return result;
#else
    const std::string exe =
        executable_path.empty() ? detail::GetCurrentExecutablePath() : executable_path;
    if (exe.empty()) {
        result.error_number = errno == 0 ? ENOENT : errno;
        result.tag = kMsgPkexecLaunchFailed;
        result.detail = "cannot resolve executable path";
        return result;
    }

    result.manual_command = detail::FormatManualSetcapCommand(exe);
    if (detail::HasRequiredCapabilitiesByGetcap(exe, logger)) {
        result.has_capabilities = true;
        return result;
    }

    const char* escalated = std::getenv("ENCOS_FDBROKER_ESCALATED");
    if (escalated != nullptr && std::string(escalated) == "1") {
        result.error_number = EPERM;
        result.tag = kMsgAlreadyEscalated;
        result.detail = "capabilities still missing after one escalation attempt";
        return result;
    }

    if (!detail::IsCommandAvailable("pkexec")) {
        result.error_number = EPERM;
        result.tag = kMsgPkexecUnavailable;
        result.detail = "pkexec not available";
        return result;
    }

    const uid_t uid = getuid();
    const gid_t gid = getgid();
    std::string script_path;
    if (!detail::CreatePrivilegeScript("/tmp/encos_fdbroker_cap_XXXXXX", exe, uid, gid, broker_args,
                                       &script_path, logger)) {
        result.error_number = errno == 0 ? EIO : errno;
        result.tag = kMsgPkexecLaunchFailed;
        result.detail = "failed to Create privilege helper script";
        return result;
    }

    result.error_number = EPERM;
    result.escalation_pid = static_cast<int32_t>(getpid());
    result.tag = kMsgEscalateRestarting;
    result.detail = "broker will run pkexec to set capabilities and restart (pid=" +
                    std::to_string(result.escalation_pid) + ")";
    result.script_path = script_path;
    result.started_escalation = true;
    return result;
#endif
}

inline std::string FormatHandshakeMessage(const PrivilegeBootstrapResult& result) {
    std::ostringstream oss;
    oss << result.tag;
    if (!result.detail.empty()) {
        oss << "; " << result.detail;
    }
    if (!result.manual_command.empty()) {
        oss << "; manual: " << result.manual_command;
    }
    return oss.str();
}

inline bool IsEscalationRestartingMessage(const char* message) {
    if (message == nullptr) {
        return false;
    }
    const std::string m(message);
    return m.rfind(kMsgEscalateRestarting, 0) == 0;
}

}  // namespace fd_broker
}  // namespace encos
