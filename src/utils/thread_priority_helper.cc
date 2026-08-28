#include "utils/thread_priority_internal.h"

#if defined(__linux__) && !defined(__EMSCRIPTEN__)

#include <cerrno>
#include <sched.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    using encos::utils::detail::HelperExitCode;
    using encos::utils::detail::ThreadPriorityRequest;

    ThreadPriorityRequest request{};
    auto result = encos::utils::detail::ParseThreadPriorityRequest(argc, argv, request);
    if (result != HelperExitCode::kSuccess) {
        return static_cast<int>(result);
    }
    result = encos::utils::detail::ValidateThreadPriorityRequest(request, getppid(), getuid());
    if (result != HelperExitCode::kSuccess) {
        return static_cast<int>(result);
    }
    result = encos::utils::detail::ValidateThreadPriorityRequest(request, getppid(), getuid());
    if (result != HelperExitCode::kSuccess) {
        return static_cast<int>(result);
    }

    sched_param parameters{};
    parameters.sched_priority = request.priority;
    if (sched_setscheduler(request.thread_id, SCHED_FIFO, &parameters) != 0) {
        return static_cast<int>(errno == EPERM || errno == EACCES
                                    ? HelperExitCode::kPermissionDenied
                                    : HelperExitCode::kSchedulingFailed);
    }
    return static_cast<int>(HelperExitCode::kSuccess);
}

#else

int main() {
    return 64;
}

#endif
