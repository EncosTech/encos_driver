#include "utils/thread_priority_internal.h"

#if defined(__linux__) && !defined(__EMSCRIPTEN__)

#include <climits>
#include <fstream>
#include <sched.h>
#include <sstream>
#include <string>

namespace encos::utils::detail {
namespace {

bool ParsePositiveDecimal(const char* text, long maximum, long& value) {
    if (!text || *text == '\0') {
        return false;
    }
    long parsed = 0;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        const int digit = *cursor - '0';
        if (parsed > (maximum - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }
    if (parsed <= 0) {
        return false;
    }
    value = parsed;
    return true;
}

bool ReadStatusIds(const std::string& path, pid_t& tgid, uid_t& uid) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    bool found_tgid = false;
    bool found_uid = false;
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("Tgid:", 0) == 0) {
            std::istringstream values(line.substr(5));
            long parsed = 0;
            if (!(values >> parsed) || parsed <= 0 || parsed > INT_MAX) {
                return false;
            }
            tgid = static_cast<pid_t>(parsed);
            found_tgid = true;
        } else if (line.rfind("Uid:", 0) == 0) {
            std::istringstream values(line.substr(4));
            unsigned long parsed = 0;
            if (!(values >> parsed) || parsed > UINT_MAX) {
                return false;
            }
            uid = static_cast<uid_t>(parsed);
            found_uid = true;
        }
    }
    return found_tgid && found_uid;
}

}  // namespace

HelperExitCode ParseThreadPriorityRequest(int argc, const char* const argv[],
                                          ThreadPriorityRequest& request) {
    if (argc != 4) {
        return HelperExitCode::kInvalidArguments;
    }
    long process_id = 0;
    long thread_id = 0;
    long priority = 0;
    if (!ParsePositiveDecimal(argv[1], INT_MAX, process_id) ||
        !ParsePositiveDecimal(argv[2], INT_MAX, thread_id) ||
        !ParsePositiveDecimal(argv[3], INT_MAX, priority)) {
        return HelperExitCode::kInvalidArguments;
    }
    request = {static_cast<pid_t>(process_id), static_cast<pid_t>(thread_id),
               static_cast<int>(priority)};
    return HelperExitCode::kSuccess;
}

HelperExitCode ValidateThreadPriorityRequest(const ThreadPriorityRequest& request,
                                             pid_t expected_parent, uid_t expected_uid) {
    const int minimum = sched_get_priority_min(SCHED_FIFO);
    const int maximum = sched_get_priority_max(SCHED_FIFO);
    if (minimum < 0 || maximum < 0 || request.priority < minimum || request.priority > maximum) {
        return HelperExitCode::kInvalidPriority;
    }
    if (request.process_id != expected_parent) {
        return HelperExitCode::kInvalidTarget;
    }
    pid_t process_tgid = 0;
    uid_t process_uid = 0;
    pid_t thread_tgid = 0;
    uid_t thread_uid = 0;
    if (!ReadStatusIds("/proc/" + std::to_string(request.process_id) + "/status", process_tgid,
                       process_uid) ||
        !ReadStatusIds("/proc/" + std::to_string(request.process_id) + "/task/" +
                           std::to_string(request.thread_id) + "/status",
                       thread_tgid, thread_uid) ||
        process_tgid != request.process_id || thread_tgid != request.process_id ||
        process_uid != expected_uid || thread_uid != expected_uid) {
        return HelperExitCode::kInvalidTarget;
    }
    return HelperExitCode::kSuccess;
}

}  // namespace encos::utils::detail

#endif
