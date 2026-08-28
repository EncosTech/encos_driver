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
void RunSerializedPriorityRequestForTesting(const std::function<void()>& action);

}  // namespace encos::utils::detail

#endif
