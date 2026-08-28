#include "fd_broker_privilege.h"

#include <array>
#include <chrono>
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <thread>

#include "fd_broker_common.h"

#if defined(__linux__)
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace encos::fd_broker {
namespace {

TEST(FdBrokerPrivilegeTests, RestartArgumentsPreserveNonceAndExecutableIdentity) {
    std::array<std::string, 8> storage{
        "broker", "can0", "/tmp/control.sock", "info", "logger", "nonce", "42", "84"};
    std::array<char*, 8> argv{};
    for (std::size_t i = 0; i < storage.size(); ++i) {
        argv[i] = storage[i].data();
    }

    const auto arguments = BuildBrokerRestartArguments(static_cast<int>(argv.size()), argv.data());

    ASSERT_EQ(arguments.size(), 7);
    EXPECT_EQ(arguments[4], "nonce");
    EXPECT_EQ(arguments[5], "42");
    EXPECT_EQ(arguments[6], "84");
}

TEST(FdBrokerPrivilegeTests, RestartArgumentsRejectIncompleteIdentity) {
    std::array<std::string, 7> storage{"broker", "can0", "/tmp/control.sock", "info", "logger",
                                       "nonce",  "42"};
    std::array<char*, 7> argv{};
    for (std::size_t i = 0; i < storage.size(); ++i) {
        argv[i] = storage[i].data();
    }

    EXPECT_TRUE(BuildBrokerRestartArguments(static_cast<int>(argv.size()), argv.data()).empty());
}

TEST(FdBrokerPrivilegeTests, IdentityNumbersRejectInvalidTextWithoutThrowing) {
    std::uint64_t value = 0;
    EXPECT_TRUE(ParseUint64("18446744073709551615", &value));
    EXPECT_EQ(value, std::numeric_limits<std::uint64_t>::max());
    EXPECT_FALSE(ParseUint64("-1", &value));
    EXPECT_FALSE(ParseUint64("12x", &value));
    EXPECT_FALSE(ParseUint64("", &value));
}

#if defined(__linux__)
TEST(FdBrokerAuthenticationTests, DirectPhaseRequiresExactChildPidAndRealUid) {
    PeerCredentials peer{};
    peer.pid = getpid();
    peer.uid = getuid();
    struct stat executable_stat {};
    ASSERT_EQ(stat("/proc/self/exe", &executable_stat), 0);

    EXPECT_TRUE(IsAuthorizedBrokerPeer(peer, BrokerPeerPhase::Direct, getpid(), getuid(),
                                       executable_stat.st_dev, executable_stat.st_ino));
    EXPECT_FALSE(IsAuthorizedBrokerPeer(peer, BrokerPeerPhase::Direct, getpid() + 1, getuid(),
                                        executable_stat.st_dev, executable_stat.st_ino));
}

TEST(FdBrokerAuthenticationTests, EscalatedPhaseRequiresMatchingExecutableIdentity) {
    PeerCredentials peer{};
    peer.pid = getpid();
    peer.uid = getuid();
    struct stat executable_stat {};
    ASSERT_EQ(stat("/proc/self/exe", &executable_stat), 0);

    EXPECT_TRUE(IsAuthorizedBrokerPeer(peer, BrokerPeerPhase::Escalated, -1, getuid(),
                                       executable_stat.st_dev, executable_stat.st_ino));
    EXPECT_FALSE(IsAuthorizedBrokerPeer(peer, BrokerPeerPhase::Escalated, -1, getuid(),
                                        executable_stat.st_dev, executable_stat.st_ino + 1));
}

TEST(FdBrokerAuthenticationTests, ConnectedPeerCannotStallPastHandshakeDeadline) {
    int sockets[2] = {-1, -1};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    std::thread stalled_peer([fd = sockets[1]]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        close(fd);
    });

    const auto start = std::chrono::steady_clock::now();
    const auto received = ReceiveConnectedBrokerHandshake(sockets[0], 20);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(received.complete);
    EXPECT_LT(elapsed, std::chrono::milliseconds(80));
    stalled_peer.join();
}
#endif

}  // namespace
}  // namespace encos::fd_broker
