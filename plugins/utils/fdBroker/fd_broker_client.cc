#include "fd_broker_client.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__linux__)
#include <csignal>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "fd_broker_common.h"
#include "fd_broker_privilege.h"
#include "platform/delay.h"

namespace encos {
namespace fd_broker {

#if defined(__linux__)
namespace {

constexpr auto kEscalationScriptMaxWait = std::chrono::minutes(10);
constexpr auto kPostEscalationReconnectWait = std::chrono::seconds(3);

bool HasBrokerExited(pid_t pid) {
    int status = 0;
    const pid_t result = waitpid(pid, &status, WNOHANG);
    return result == pid || (result < 0 && errno == ECHILD);
}

pid_t SpawnBrokerProcess(const std::string& broker_executable, const std::vector<std::string>& args,
                         LoggerPtr logger) {
    pid_t pid = fork();
    if (pid < 0) {
        logger->error("Failed to fork fd broker '{}': errno={}", broker_executable, errno);
        return -1;
    }
    if (pid == 0) {
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(broker_executable.c_str(), argv.data());
        _exit(127);
    }
    return pid;
}

bool WaitBrokerExit(pid_t pid, std::chrono::milliseconds timeout, int* exit_code) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        int status = 0;
        const pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            if (WIFEXITED(status)) {
                *exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                *exit_code = 128 + WTERMSIG(status);
            } else {
                *exit_code = -1;
            }
            return true;
        }
        if (ret < 0) {
            if (errno == ECHILD) {
                *exit_code = 0;
                return true;
            }
            *exit_code = -1;
            return true;
        }
        platform::SleepFor(std::chrono::milliseconds(10));
    }
    return false;
}

void KillAndReapBroker(pid_t pid) {
    if (pid <= 0) {
        return;
    }
    (void) kill(pid, SIGKILL);
    int status = 0;
    (void) waitpid(pid, &status, 0);
}

}  // namespace
#endif

int FdBrokerClient::RequestFd(const std::string& broker_executable,
                              const std::string& interface_name, const std::string& socket_prefix,
                              LoggerPtr logger, std::chrono::milliseconds timeout) {
#if !defined(__linux__)
    (void) broker_executable;
    (void) interface_name;
    (void) socket_prefix;
    (void) logger;
    (void) timeout;
    return -1;
#else
    const auto logger_level = LogLevelToString(logger->Level());
    const std::string child_logger_name = logger->Name() + ".FdBroker";
    std::array<uint8_t, 32> nonce{};
    if (!GenerateNonce(&nonce)) {
        logger->error("Failed to generate a secure fd broker nonce.");
        return -1;
    }
    const std::string encoded_nonce = EncodeNonce(nonce);
    struct stat broker_stat {};
    if (stat(broker_executable.c_str(), &broker_stat) != 0) {
        logger->error("Failed to stat fd broker executable '{}': errno={}", broker_executable,
                      errno);
        return -1;
    }

    const std::string socket_path = GenerateSocketPath(socket_prefix);
    if (socket_path.empty()) {
        logger->error("No free Unix socket path for fd broker '{}'.", socket_prefix);
        return -1;
    }

    const int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        logger->error("Failed to Create fd broker listen socket: errno={}", errno);
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(socket_path).parent_path(), ec);
        return -1;
    }

    const auto socket_directory = std::filesystem::path(socket_path).parent_path();
    auto cleanup = [&]() {
        close(listen_fd);
        std::error_code ec;
        std::filesystem::remove(socket_path, ec);
        std::filesystem::remove(socket_directory, ec);
    };

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path.c_str());

    std::error_code remove_err;
    std::filesystem::remove(socket_path, remove_err);
    if (bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        logger->error("Failed to bind fd broker socket '{}': errno={}", socket_path, errno);
        cleanup();
        return -1;
    }
    if (listen(listen_fd, 1) < 0) {
        logger->error("Failed to listen on fd broker socket '{}': errno={}", socket_path, errno);
        cleanup();
        return -1;
    }

    ENCOS_LOG_DEBUG(logger, "Launching fd broker '{}' for interface '{}' on socket '{}'.",
                    broker_executable, interface_name, socket_path);

    const pid_t broker_pid = SpawnBrokerProcess(
        broker_executable,
        std::vector<std::string>{broker_executable, interface_name, socket_path, logger_level,
                                 child_logger_name, encoded_nonce,
                                 std::to_string(static_cast<uint64_t>(broker_stat.st_dev)),
                                 std::to_string(static_cast<uint64_t>(broker_stat.st_ino))},
        logger);
    if (broker_pid <= 0) {
        cleanup();
        return -1;
    }

    auto deadline = std::chrono::steady_clock::now() + timeout;
    Handshake last_handshake{};
    last_handshake.status = kStatusError;
    last_handshake.error_number = ETIMEDOUT;
    last_handshake.escalation_pid = 0;
    last_handshake.nonce = nonce;
    std::snprintf(last_handshake.message, sizeof(last_handshake.message), "%s",
                  "timeout waiting for fd broker handshake");

    int raw_fd = -1;
    bool handshake_ok = false;
    bool escalation_observed = false;
    BrokerPeerPhase peer_phase = BrokerPeerPhase::Direct;
    pid_t escalation_pid = -1;
    bool escalation_exit_window_started = false;
    const uid_t real_uid = getuid();
    const auto executable_device = static_cast<std::uint64_t>(broker_stat.st_dev);
    const auto executable_inode = static_cast<std::uint64_t>(broker_stat.st_ino);

    auto observe_escalation_exit = [&]() {
        if (peer_phase != BrokerPeerPhase::Escalated || escalation_exit_window_started ||
            !HasBrokerExited(broker_pid)) {
            return;
        }
        const auto reconnect_deadline =
            std::chrono::steady_clock::now() + kPostEscalationReconnectWait;
        if (reconnect_deadline < deadline) {
            deadline = reconnect_deadline;
        }
        escalation_exit_window_started = true;
        logger->warn(
            "fd broker escalation helper exited for '{}', waiting {} seconds for reconnect.",
            interface_name,
            std::chrono::duration_cast<std::chrono::seconds>(kPostEscalationReconnectWait).count());
    };

    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            break;
        }

        auto receive_wait = remaining;
        if (peer_phase == BrokerPeerPhase::Escalated) {
            receive_wait = std::min(receive_wait, std::chrono::milliseconds(100));
        }
        const auto received =
            ReceiveBrokerHandshake(listen_fd, static_cast<int>(receive_wait.count()));
        if (!received.complete) {
            observe_escalation_exit();
            continue;
        }
        const auto& handshake = received.handshake;
        last_handshake = handshake;

        if (handshake.nonce != nonce) {
            logger->error("fd broker handshake nonce mismatch for interface '{}'.", interface_name);
            if (received.fd >= 0) {
                close(received.fd);
            }
            continue;
        }

        if (!IsAuthorizedBrokerPeer(received.peer, peer_phase, broker_pid, real_uid,
                                    executable_device, executable_inode)) {
            logger->error("Rejected unauthenticated fd broker peer pid={} uid={} for '{}'.",
                          received.peer.pid, received.peer.uid, interface_name);
            if (received.fd >= 0) {
                close(received.fd);
            }
            observe_escalation_exit();
            continue;
        }

        if (received.fd >= 0 && handshake.status == kStatusOk) {
            raw_fd = received.fd;
            handshake_ok = true;
            break;
        }

        if (received.fd >= 0) {
            close(received.fd);
        }

        if (peer_phase == BrokerPeerPhase::Direct && handshake.status == kStatusError &&
            handshake.error_number == EPERM && IsEscalationRestartingMessage(handshake.message)) {
            escalation_observed = true;
            if (handshake.escalation_pid <= 0) {
                logger->error(
                    "fd broker escalation did not provide a valid helper pid for interface '{}'.",
                    interface_name);
                break;
            }

            escalation_pid = static_cast<pid_t>(handshake.escalation_pid);
            peer_phase = BrokerPeerPhase::Escalated;
            deadline = std::chrono::steady_clock::now() + kEscalationScriptMaxWait;
            logger->warn("fd broker for '{}' entered escalation wait state (helper pid {}).",
                         interface_name, handshake.escalation_pid);
            observe_escalation_exit();
            continue;
        }

        if (handshake.status == kStatusError && handshake.message[0] != '\0') {
            break;
        }
        observe_escalation_exit();
    }

    if (!handshake_ok) {
        if (escalation_observed && last_handshake.error_number == ETIMEDOUT) {
            logger->error("fd broker escalation timed out for interface '{}'.", interface_name);
        }
        logger->error("fd broker failed for interface '{}': status={}, errno={}, message='{}'",
                      interface_name, static_cast<int>(last_handshake.status),
                      last_handshake.error_number, last_handshake.message);

        int rc = 0;
        if (!WaitBrokerExit(broker_pid, timeout, &rc)) {
            KillAndReapBroker(broker_pid);
        }

        cleanup();
        return -1;
    }

    int rc = 0;
    if (!WaitBrokerExit(broker_pid, timeout, &rc)) {
        logger->warn("fd broker process did not exit in time, forcing kill.");
        KillAndReapBroker(broker_pid);
    } else if (rc != 0) {
        logger->warn("fd broker exited with non-zero code {} after fd handoff.", rc);
    }

    cleanup();
    ENCOS_LOG_DEBUG(logger, "Received socket fd {} from broker '{}'.", raw_fd, broker_executable);
    return raw_fd;
#endif
}

}  // namespace fd_broker
}  // namespace encos
