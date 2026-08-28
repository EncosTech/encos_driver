#include "utils/thread_priority.h"

#include <gtest/gtest.h>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <sched.h>
#include <sstream>
#include <string>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "utils/thread_priority_internal.h"

namespace encos::utils::detail {
namespace {

int RunHelper(const std::vector<std::string>& arguments) {
    const pid_t child = fork();
    if (child == 0) {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(ENCOS_TEST_THREAD_PRIORITY_HELPER));
        for (const auto& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        execv(ENCOS_TEST_THREAD_PRIORITY_HELPER, argv.data());
        _exit(127);
    }
    if (child < 0) {
        return -1;
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
}

bool HasEffectiveSysNiceCapability() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("CapEff:", 0) != 0) {
            continue;
        }
        std::uint64_t capabilities = 0;
        std::istringstream value(line.substr(7));
        value >> std::hex >> capabilities;
        constexpr std::uint64_t kCapSysNiceMask = std::uint64_t{1} << 23u;
        return (capabilities & kCapSysNiceMask) != 0;
    }
    return false;
}

TEST(ThreadPriorityHelperTests, ParsesOnlyExactPositiveDecimalArguments) {
    ThreadPriorityRequest request{};
    const char* valid[] = {"helper", "123", "456", "50"};
    EXPECT_EQ(ParseThreadPriorityRequest(4, valid, request), HelperExitCode::kSuccess);
    EXPECT_EQ(request.process_id, 123);
    EXPECT_EQ(request.thread_id, 456);
    EXPECT_EQ(request.priority, 50);

    const char* signed_value[] = {"helper", "+123", "456", "50"};
    EXPECT_EQ(ParseThreadPriorityRequest(4, signed_value, request),
              HelperExitCode::kInvalidArguments);
    const char* trailing[] = {"helper", "123x", "456", "50"};
    EXPECT_EQ(ParseThreadPriorityRequest(4, trailing, request), HelperExitCode::kInvalidArguments);
    EXPECT_EQ(ParseThreadPriorityRequest(3, valid, request), HelperExitCode::kInvalidArguments);
}

TEST(ThreadPriorityHelperTests, RejectsPriorityOutsideSystemFifoRange) {
    ThreadPriorityRequest request{getpid(), static_cast<pid_t>(syscall(SYS_gettid)), 0};
    EXPECT_EQ(ValidateThreadPriorityRequest(request, getpid(), getuid()),
              HelperExitCode::kInvalidPriority);

    request.priority = sched_get_priority_max(SCHED_FIFO) + 1;
    EXPECT_EQ(ValidateThreadPriorityRequest(request, getpid(), getuid()),
              HelperExitCode::kInvalidPriority);
}

TEST(ThreadPriorityHelperTests, RequiresDirectParentAndThreadMembership) {
    ThreadPriorityRequest request{getpid(), static_cast<pid_t>(syscall(SYS_gettid)), 50};
    EXPECT_EQ(ValidateThreadPriorityRequest(request, getpid() + 1, getuid()),
              HelperExitCode::kInvalidTarget);

    request.thread_id = static_cast<pid_t>(999999999);
    EXPECT_EQ(ValidateThreadPriorityRequest(request, getpid(), getuid()),
              HelperExitCode::kInvalidTarget);
}

TEST(ThreadPriorityHelperTests, ExecutableRejectsMalformedAndExternalTargets) {
    EXPECT_EQ(RunHelper({"1", "2"}), static_cast<int>(HelperExitCode::kInvalidArguments));
    EXPECT_EQ(RunHelper({"1", "1", "50"}), static_cast<int>(HelperExitCode::kInvalidTarget));
}

TEST(ThreadPriorityHelperTests, ExecutableFailsClosedWithoutCapability) {
    if (HasEffectiveSysNiceCapability()) {
        GTEST_SKIP() << "Current test process already has effective CAP_SYS_NICE";
    }
    const auto process_id = std::to_string(getpid());
    const auto thread_id = std::to_string(static_cast<pid_t>(syscall(SYS_gettid)));
    EXPECT_EQ(RunHelper({process_id, thread_id, "50"}),
              static_cast<int>(HelperExitCode::kPermissionDenied));
}

TEST(ThreadPriorityHelperTests, KeepsOpenedInodeWhenPathIsReplaced) {
    char first_path[] = "/tmp/encos-priority-helper-a-XXXXXX";
    const int first = mkstemp(first_path);
    ASSERT_GE(first, 0);
    ASSERT_EQ(fchmod(first, 0700), 0);
    close(first);

    auto pinned = OpenPinnedExecutable(first_path);
    ASSERT_TRUE(pinned);
    const auto identity = pinned->identity();

    char replacement_path[] = "/tmp/encos-priority-helper-b-XXXXXX";
    const int replacement = mkstemp(replacement_path);
    ASSERT_GE(replacement, 0);
    ASSERT_EQ(fchmod(replacement, 0700), 0);
    close(replacement);
    ASSERT_EQ(rename(replacement_path, first_path), 0);

    EXPECT_EQ(pinned->identity(), identity);
    EXPECT_FALSE(pinned->MatchesPath(first_path));
    unlink(first_path);
}

TEST(ThreadPriorityHelperTests, PreservesVerifiedPathForCapabilityGrant) {
    char helper_path[] = "/tmp/encos-priority-helper-path-XXXXXX";
    const int helper_file = mkstemp(helper_path);
    ASSERT_GE(helper_file, 0);
    ASSERT_EQ(fchmod(helper_file, 0700), 0);
    close(helper_file);

    auto pinned = OpenPinnedExecutable(helper_path);
    ASSERT_TRUE(pinned);

    EXPECT_EQ(pinned->path(), helper_path);
    EXPECT_TRUE(pinned->MatchesPath(pinned->path()));
    unlink(helper_path);
}

TEST(ThreadPriorityHelperTests, SerializesConcurrentRequests) {
    std::atomic<int> active{0};
    std::atomic<int> maximum{0};
    std::vector<std::thread> threads;
    for (int index = 0; index < 8; ++index) {
        threads.emplace_back([&]() {
            RunSerializedPriorityRequestForTesting([&]() {
                const int now = active.fetch_add(1) + 1;
                int observed = maximum.load();
                while (observed < now && !maximum.compare_exchange_weak(observed, now)) {}
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                active.fetch_sub(1);
            });
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(maximum.load(), 1);
}

}  // namespace
}  // namespace encos::utils::detail
#else
TEST(ThreadPriorityTests, UnsupportedPlatformDoesNotElevate) {
    EXPECT_FALSE(encos::utils::SetCurrentThreadPriority(50));
}
#endif
