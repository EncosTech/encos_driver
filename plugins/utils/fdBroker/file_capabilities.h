#pragma once

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/capability.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;
namespace encos::fd_broker {
/** @brief 检查 capability 集合是否包含所有所需的有效、允许权限。 */
inline bool HasCapabilities(cap_t caps, const std::vector<cap_value_t>& required) {
    if (caps == nullptr)
        return false;
    for (auto value : required) {
        cap_flag_value_t permitted = CAP_CLEAR, effective = CAP_CLEAR;
        if (cap_get_flag(caps, value, CAP_PERMITTED, &permitted) != 0 ||
            cap_get_flag(caps, value, CAP_EFFECTIVE, &effective) != 0 || permitted != CAP_SET ||
            effective != CAP_SET)
            return false;
    }
    return true;
}
/** @brief 检查辅助程序文件的 capabilities。 */
inline bool HasFileCapabilities(const std::string& path, const std::vector<cap_value_t>& required) {
    cap_t caps = cap_get_file(path.c_str());
    const bool present = HasCapabilities(caps, required);
    if (caps != nullptr)
        cap_free(caps);
    return present;
}
/** @brief 缺少权限时通过 Polkit 给辅助程序文件授权，随后仍以普通用户启动。 */
inline void EnsureFileCapabilities(const std::string& path,
                                   const std::vector<cap_value_t>& required,
                                   const std::string& specification) {
    if (geteuid() == 0)
        return;
    const auto executable = std::filesystem::canonical(path).string();
    if (HasFileCapabilities(executable, required))
        return;
    const char* setcap =
        access("/usr/sbin/setcap", X_OK) == 0 ? "/usr/sbin/setcap" : "/sbin/setcap";
    if (access(setcap, X_OK) != 0)
        throw std::runtime_error("setcap unavailable; install libcap2-bin");
    std::vector<char*> arguments{const_cast<char*>("pkexec"), const_cast<char*>(setcap),
                                 const_cast<char*>(specification.c_str()),
                                 const_cast<char*>(executable.c_str()), nullptr};
    pid_t child = -1;
    const auto result =
        posix_spawn(&child, "/usr/bin/pkexec", nullptr, nullptr, arguments.data(), environ);
    if (result != 0)
        throw std::runtime_error("Unable to start Polkit authorization; install pkexec");
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("Helper capability authorization cancelled or failed");
    if (!HasFileCapabilities(executable, required))
        throw std::runtime_error("Helper capabilities missing after authorization");
}
}  // namespace encos::fd_broker
