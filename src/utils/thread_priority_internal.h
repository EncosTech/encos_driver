// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#if defined(__linux__) && !defined(__EMSCRIPTEN__)

#include <functional>
#include <optional>
#include <string>
#include <sys/types.h>

namespace encos::utils::detail {

enum class HelperExitCode : int {
    kSuccess = 0,
    kInvalidArguments = 64,
    kInvalidPriority = 65,
    kInvalidTarget = 66,
    kPermissionDenied = 77,
    kSchedulingFailed = 78,
};

struct ThreadPriorityRequest {
    pid_t process_id = 0;
    pid_t thread_id = 0;
    int priority = 0;
};

struct FileIdentity {
    dev_t device = 0;
    ino_t inode = 0;

    bool operator==(const FileIdentity& other) const {
        return device == other.device && inode == other.inode;
    }
};

class PinnedExecutable {
public:
    PinnedExecutable(int fd, FileIdentity identity, std::string path);
    ~PinnedExecutable();
    PinnedExecutable(PinnedExecutable&& other) noexcept;
    PinnedExecutable& operator=(PinnedExecutable&& other) noexcept;
    PinnedExecutable(const PinnedExecutable&) = delete;
    PinnedExecutable& operator=(const PinnedExecutable&) = delete;

    int fd() const;
    FileIdentity identity() const;
    const std::string& path() const;
    bool MatchesPath(const std::string& path) const;

private:
    int fd_ = -1;
    FileIdentity identity_{};
    std::string path_;
};

HelperExitCode ParseThreadPriorityRequest(int argc, const char* const argv[],
                                          ThreadPriorityRequest& request);
HelperExitCode ValidateThreadPriorityRequest(const ThreadPriorityRequest& request,
                                             pid_t expected_parent, uid_t expected_uid);
std::optional<PinnedExecutable> OpenPinnedExecutable(const std::string& path);

/** @brief 串行执行调度设置与锁页，分别报告错误；回调用于无提权测试。 */
void RunThreadPrioritySetup(const std::function<void()>& set_priority,
                            const std::function<int()>& lock_memory);

/**
 * @brief 解析 Linux 进程锁页策略。
 * @param policy `ENCOS_MEMORY_LOCK` 的值，未设置时传入 `nullptr`。
 * @return 未设置或为 `all` 时返回 `true`，为 `none` 时返回 `false`。
 * @throws MemoryLockError 策略值无效。
 */
bool ShouldLockProcessMemory(const char* policy);

void RunSerializedPriorityRequestForTesting(const std::function<void()>& action);

}  // namespace encos::utils::detail

#endif
