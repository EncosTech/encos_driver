#include <array>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
#include <csignal>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include "can_socket_setup.h"
#include "fd_broker_common.h"
#include "fd_broker_privilege.h"
#include "platform/log.h"

int main(int argc, char** argv) {
#if defined(__linux__)
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
        return 1;
    }
    if (getppid() == 1) {
        return 1;
    }
#endif

    if (argc < 8) {
        return 1;
    }

    std::string ifname = argv[1];
    std::string socket_path = argv[2];
    std::array<uint8_t, 32> nonce{};
    const bool valid_nonce = encos::fd_broker::DecodeNonce(argv[5], &nonce);
    std::uint64_t expected_device = 0;
    std::uint64_t expected_inode = 0;
    struct stat executable_stat {};
    const bool valid_identity = encos::fd_broker::ParseUint64(argv[6], &expected_device) &&
                                encos::fd_broker::ParseUint64(argv[7], &expected_inode) &&
                                stat("/proc/self/exe", &executable_stat) == 0 &&
                                static_cast<uint64_t>(executable_stat.st_dev) == expected_device &&
                                static_cast<uint64_t>(executable_stat.st_ino) == expected_inode;
    encos::LogLevel log_level = encos::LogLevel::Info;
    std::string logger_name = "CanFdBroker";

    if (argc >= 4) {
        log_level = encos::LogLevelFromString(argv[3]);
    }
    if (argc >= 5) {
        logger_name = argv[4];
    }

    auto logger = encos::CreateLogger(logger_name, log_level);

#if !defined(__linux__)
    (void) ifname;
    (void) socket_path;
    logger->error("CanFdBrokerExecutable is supported only on Linux.");
    return 1;
#else
    int control_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (control_fd < 0) {
        logger->error("Failed to create control socket: errno={}", errno);
        return 1;
    }

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path.c_str());

    if (connect(control_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        logger->error("Failed to connect to parent socket '{}': errno={}", socket_path, errno);
        close(control_fd);
        return 1;
    }

    encos::fd_broker::Handshake handshake{};
    std::memset(&handshake, 0, sizeof(handshake));

    const auto broker_args = encos::fd_broker::BuildBrokerRestartArguments(argc, argv);

    handshake.nonce = nonce;
    if (!valid_nonce || !valid_identity) {
        encos::fd_broker::FillError(&handshake, EINVAL, "invalid fd broker handshake nonce");
        (void) encos::fd_broker::SendFdWithHandshake(control_fd, handshake, -1);
        close(control_fd);
        return 1;
    }

    const auto bootstrap =
        encos::fd_broker::EnsureBrokerCapabilitiesOrEscalate("", broker_args, logger);
    if (!bootstrap.has_capabilities) {
        encos::fd_broker::FillError(&handshake,
                                    bootstrap.error_number == 0 ? EPERM : bootstrap.error_number,
                                    encos::fd_broker::FormatHandshakeMessage(bootstrap));
        handshake.escalation_pid = bootstrap.escalation_pid;
        if (!bootstrap.manual_command.empty()) {
            logger->error("manual: {}", bootstrap.manual_command);
        }
        (void) encos::fd_broker::SendFdWithHandshake(control_fd, handshake, -1);
        close(control_fd);
        if (bootstrap.started_escalation) {
            encos::fd_broker::detail::ReplaceBrokerWithPkexecScript(bootstrap.script_path);
        }
        return 1;
    }

    if (!encos::can::EnsureCanInterfaceReady(ifname, logger)) {
        encos::fd_broker::FillError(&handshake, errno == 0 ? EPERM : errno,
                                    "failed to initialize can interface");
        (void) encos::fd_broker::SendFdWithHandshake(control_fd, handshake, -1);
        close(control_fd);
        return 1;
    }

    const int can_fd = encos::can::CreateConfiguredCanSocket(ifname);
    if (can_fd < 0) {
        encos::fd_broker::FillError(&handshake, errno, "failed to configure can socket");
        logger->error("Failed to create CAN socket for interface '{}': errno={}", ifname, errno);
        (void) encos::fd_broker::SendFdWithHandshake(control_fd, handshake, -1);
        close(control_fd);
        return 1;
    }

    handshake.status = encos::fd_broker::kStatusOk;
    handshake.error_number = 0;
    handshake.escalation_pid = 0;
    std::snprintf(handshake.message, sizeof(handshake.message), "%s", "ok");

    if (encos::fd_broker::SendFdWithHandshake(control_fd, handshake, can_fd) != 0) {
        logger->error("Failed to send fd to parent process: errno={}", errno);
        close(can_fd);
        close(control_fd);
        return 1;
    }

    close(can_fd);
    close(control_fd);
    return 0;
#endif
}
