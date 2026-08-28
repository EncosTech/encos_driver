#include "utils/thread_priority.h"

#if defined(__linux__) && !defined(__EMSCRIPTEN__)

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <string>
#include <sys/capability.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "platform/sync.h"
#include "utils/thread_priority_internal.h"

#ifndef ENCOS_THREAD_PRIORITY_HELPER_BUILD_PATH
#define ENCOS_THREAD_PRIORITY_HELPER_BUILD_PATH ""
#endif
#ifndef ENCOS_THREAD_PRIORITY_HELPER_INSTALL_PATH
#define ENCOS_THREAD_PRIORITY_HELPER_INSTALL_PATH ""
#endif
#ifndef ENCOS_PKEXEC_PATH
#define ENCOS_PKEXEC_PATH ""
#endif
#ifndef ENCOS_SETCAP_PATH
#define ENCOS_SETCAP_PATH ""
#endif

namespace encos::utils::detail {
namespace {

platform::Mutex priority_request_mutex;

std::string DirectoryName(const std::string& path) {
    const auto separator = path.find_last_of('/');
    return separator == std::string::npos ? "." : path.substr(0, separator);
}

std::vector<std::string> HelperCandidates() {
    std::vector<std::string> candidates;
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&SetCurrentThreadPriority), &info) != 0 &&
        info.dli_fname) {
        candidates.push_back(DirectoryName(info.dli_fname) + "/encosPlugins/ThreadPriorityHelper");
    }
    if (!std::string(ENCOS_THREAD_PRIORITY_HELPER_BUILD_PATH).empty()) {
        candidates.emplace_back(ENCOS_THREAD_PRIORITY_HELPER_BUILD_PATH);
    }
    if (!std::string(ENCOS_THREAD_PRIORITY_HELPER_INSTALL_PATH).empty()) {
        candidates.emplace_back(ENCOS_THREAD_PRIORITY_HELPER_INSTALL_PATH);
    }
    return candidates;
}

std::optional<PinnedExecutable> OpenConfiguredHelper() {
    for (const auto& path : HelperCandidates()) {
        auto helper = OpenPinnedExecutable(path);
        if (helper) {
            return helper;
        }
    }
    return std::nullopt;
}

bool HasRequiredCapability(int fd) {
    cap_t capabilities = cap_get_fd(fd);
    if (!capabilities) {
        return false;
    }
    cap_flag_value_t permitted = CAP_CLEAR;
    cap_flag_value_t effective = CAP_CLEAR;
    const bool valid = cap_get_flag(capabilities, CAP_SYS_NICE, CAP_PERMITTED, &permitted) == 0 &&
                       cap_get_flag(capabilities, CAP_SYS_NICE, CAP_EFFECTIVE, &effective) == 0;
    cap_free(capabilities);
    return valid && permitted == CAP_SET && effective == CAP_SET;
}

bool HasCurrentProcessCapability(cap_value_t capability) {
    cap_t capabilities = cap_get_proc();
    if (!capabilities) {
        return false;
    }
    cap_flag_value_t permitted = CAP_CLEAR;
    cap_flag_value_t effective = CAP_CLEAR;
    const bool valid = cap_get_flag(capabilities, capability, CAP_PERMITTED, &permitted) == 0 &&
                       cap_get_flag(capabilities, capability, CAP_EFFECTIVE, &effective) == 0;
    cap_free(capabilities);
    return valid && permitted == CAP_SET && effective == CAP_SET;
}

int WaitForChild(pid_t child, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            return WIFEXITED(status) ? WEXITSTATUS(status)
                                     : static_cast<int>(HelperExitCode::kSchedulingFailed);
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return static_cast<int>(HelperExitCode::kSchedulingFailed);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    (void) kill(child, SIGKILL);
    (void) waitpid(child, &status, 0);
    return static_cast<int>(HelperExitCode::kSchedulingFailed);
}

void CloseDescriptorsExcept(int preserved_fd, long maximum_fd) {
#ifdef SYS_close_range
    const unsigned int first_to_close = preserved_fd == 3 ? 4U : 3U;
    if (syscall(SYS_close_range, first_to_close, ~0U, 0U) == 0) {
        return;
    }
#endif
    for (int fd = 3; fd < maximum_fd; ++fd) {
        if (fd != preserved_fd) {
            close(fd);
        }
    }
}

bool SetCurrentThreadPriorityDirectly(int priority) {
    const int minimum = sched_get_priority_min(SCHED_FIFO);
    const int maximum = sched_get_priority_max(SCHED_FIFO);
    if (minimum < 0 || maximum < 0 || priority < minimum || priority > maximum) {
        return false;
    }
    if (!HasCurrentProcessCapability(CAP_SYS_NICE)) {
        return false;
    }
    sched_param parameters{};
    parameters.sched_priority = priority;
    return sched_setscheduler(0, SCHED_FIFO, &parameters) == 0;
}

bool LockCurrentProcessMemory() {
    return mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
}

int ExecutePinnedHelper(const PinnedExecutable& helper, const ThreadPriorityRequest& request) {
    const std::string process_id = std::to_string(request.process_id);
    const std::string thread_id = std::to_string(request.thread_id);
    const std::string priority = std::to_string(request.priority);
    const long maximum_fd = std::max<long>(sysconf(_SC_OPEN_MAX), 1024);
    const pid_t child = fork();
    if (child == 0) {
        int executable_fd = helper.fd();
        if (executable_fd != 3) {
            executable_fd = dup2(executable_fd, 3);
        }
        if (executable_fd < 0) {
            _exit(static_cast<int>(HelperExitCode::kSchedulingFailed));
        }
        CloseDescriptorsExcept(executable_fd, maximum_fd);
        char* const arguments[] = {
            const_cast<char*>("ThreadPriorityHelper"), const_cast<char*>(process_id.c_str()),
            const_cast<char*>(thread_id.c_str()), const_cast<char*>(priority.c_str()), nullptr};
        char* const environment[] = {const_cast<char*>("PATH=/usr/sbin:/usr/bin:/sbin:/bin"),
                                     nullptr};
        fexecve(executable_fd, arguments, environment);
        _exit(static_cast<int>(HelperExitCode::kSchedulingFailed));
    }
    if (child < 0) {
        return static_cast<int>(HelperExitCode::kSchedulingFailed);
    }
    return WaitForChild(child, std::chrono::seconds(5));
}

bool GrantCapability(const PinnedExecutable& helper) {
    const std::string pkexec = ENCOS_PKEXEC_PATH;
    const std::string setcap = ENCOS_SETCAP_PATH;
    if (pkexec.empty() || setcap.empty()) {
        return false;
    }
    if (!helper.MatchesPath(helper.path())) {
        return false;
    }
    const std::string& target = helper.path();
    const long maximum_fd = std::max<long>(sysconf(_SC_OPEN_MAX), 1024);
    const pid_t child = fork();
    if (child == 0) {
        CloseDescriptorsExcept(-1, maximum_fd);
        char* const arguments[] = {
            const_cast<char*>(pkexec.c_str()), const_cast<char*>(setcap.c_str()),
            const_cast<char*>("cap_sys_nice=ep"), const_cast<char*>(target.c_str()), nullptr};
        char* const environment[] = {const_cast<char*>("PATH=/usr/sbin:/usr/bin:/sbin:/bin"),
                                     nullptr};
        execve(pkexec.c_str(), arguments, environment);
        _exit(127);
    }
    return child > 0 && WaitForChild(child, std::chrono::minutes(10)) == 0 &&
           helper.MatchesPath(target) && HasRequiredCapability(helper.fd());
}

}  // namespace

PinnedExecutable::PinnedExecutable(int fd, FileIdentity identity, std::string path)
    : fd_(fd), identity_(identity), path_(std::move(path)) {}

PinnedExecutable::~PinnedExecutable() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

PinnedExecutable::PinnedExecutable(PinnedExecutable&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)),
      identity_(other.identity_),
      path_(std::move(other.path_)) {}

PinnedExecutable& PinnedExecutable::operator=(PinnedExecutable&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = std::exchange(other.fd_, -1);
        identity_ = other.identity_;
        path_ = std::move(other.path_);
    }
    return *this;
}

int PinnedExecutable::fd() const {
    return fd_;
}

FileIdentity PinnedExecutable::identity() const {
    return identity_;
}

const std::string& PinnedExecutable::path() const {
    return path_;
}

bool PinnedExecutable::MatchesPath(const std::string& path) const {
    struct stat status {};
    return stat(path.c_str(), &status) == 0 &&
           FileIdentity{status.st_dev, status.st_ino} == identity_;
}

std::optional<PinnedExecutable> OpenPinnedExecutable(const std::string& path) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return std::nullopt;
    }
    struct stat status {};
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
        (status.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0) {
        close(fd);
        return std::nullopt;
    }
    return PinnedExecutable(fd, {status.st_dev, status.st_ino}, path);
}

void RunSerializedPriorityRequestForTesting(const std::function<void()>& action) {
    platform::LockGuard<platform::Mutex> lock(priority_request_mutex);
    action();
}

}  // namespace encos::utils::detail

namespace encos::utils {

bool SetCurrentThreadPriority(int priority) {
    bool success = false;
    detail::RunSerializedPriorityRequestForTesting([&]() {
#ifdef ENCOS_STATIC_MODE
        success = detail::SetCurrentThreadPriorityDirectly(priority);
#else
        detail::ThreadPriorityRequest request{getpid(), static_cast<pid_t>(syscall(SYS_gettid)),
                                              priority};
        if (detail::ValidateThreadPriorityRequest(request, getpid(), getuid()) !=
            detail::HelperExitCode::kSuccess) {
            return;
        }
        auto helper = detail::OpenConfiguredHelper();
        if (!helper) {
            return;
        }
        if (!detail::HasRequiredCapability(helper->fd())) {
            const char* disable_gui = std::getenv("ENCOS_DISABLE_PRIORITY_GUI");
            if ((disable_gui && std::string(disable_gui) == "1") ||
                !detail::GrantCapability(*helper)) {
                return;
            }
        }
        success = detail::ExecutePinnedHelper(*helper, request) ==
                  static_cast<int>(detail::HelperExitCode::kSuccess);
#endif
        if (success) {
            success = detail::LockCurrentProcessMemory();
        }
    });
    return success;
}

}  // namespace encos::utils

#else

namespace encos::utils {

bool SetCurrentThreadPriority(int) {
    return false;
}

}  // namespace encos::utils

#endif
