#pragma once

#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <poll.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace encos {
namespace fd_broker {

constexpr uint8_t kStatusOk = 0;
constexpr uint8_t kStatusError = 1;
constexpr int kMaxProbeAttempts = 1000;

struct Handshake {
    uint8_t status;
    int32_t error_number;
    int32_t escalation_pid;
    std::array<uint8_t, 32> nonce{};
    char message[192];
};

inline bool GenerateNonce(std::array<uint8_t, 32>* nonce) {
    if (nonce == nullptr) {
        return false;
    }
    nonce->fill(0);
#if defined(__linux__)
    size_t offset = 0;
    while (offset < nonce->size()) {
        const ssize_t count = ::getrandom(nonce->data() + offset, nonce->size() - offset, 0);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        offset += static_cast<size_t>(count);
    }
    if (offset == nonce->size()) {
        return true;
    }
    const int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        while (offset < nonce->size()) {
            const ssize_t count = ::read(fd, nonce->data() + offset, nonce->size() - offset);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                break;
            }
            offset += static_cast<size_t>(count);
        }
        ::close(fd);
    }
    return offset == nonce->size();
#else
    return false;
#endif
}

inline bool ParseUint64(const char* text, std::uint64_t* value) {
    if (text == nullptr || value == nullptr || *text == '\0') {
        return false;
    }
    const char* end = text + std::strlen(text);
    const auto parsed = std::from_chars(text, end, *value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

inline std::string EncodeNonce(const std::array<uint8_t, 32>& nonce) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(nonce.size() * 2);
    for (const uint8_t byte : nonce) {
        encoded.push_back(kHex[byte >> 4]);
        encoded.push_back(kHex[byte & 0x0f]);
    }
    return encoded;
}

inline bool DecodeNonce(const std::string& encoded, std::array<uint8_t, 32>* nonce) {
    if (nonce == nullptr || encoded.size() != nonce->size() * 2) {
        return false;
    }
    auto decode = [](char value) -> int {
        if (value >= '0' && value <= '9')
            return value - '0';
        if (value >= 'a' && value <= 'f')
            return value - 'a' + 10;
        if (value >= 'A' && value <= 'F')
            return value - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < nonce->size(); ++i) {
        const int high = decode(encoded[i * 2]);
        const int low = decode(encoded[i * 2 + 1]);
        if (high < 0 || low < 0)
            return false;
        (*nonce)[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

inline void FillError(Handshake* handshake, int error_number, const std::string& message) {
    if (handshake == nullptr) {
        return;
    }
    handshake->status = kStatusError;
    handshake->error_number = error_number;
    std::memset(handshake->message, 0, sizeof(handshake->message));
    std::snprintf(handshake->message, sizeof(handshake->message), "%s", message.c_str());
}

#if defined(__linux__)
enum class BrokerPeerPhase {
    Direct,
    Escalated,
};

struct PeerCredentials {
    pid_t pid = -1;
    uid_t uid = static_cast<uid_t>(-1);
};

struct ReceivedBrokerHandshake {
    Handshake handshake{};
    PeerCredentials peer{};
    int fd = -1;
    bool complete = false;
};

inline bool IsAuthorizedBrokerPeer(const PeerCredentials& peer, BrokerPeerPhase phase,
                                   pid_t direct_broker_pid, uid_t real_uid,
                                   std::uint64_t executable_device,
                                   std::uint64_t executable_inode) {
    if (peer.pid <= 0) {
        return false;
    }
    if (phase == BrokerPeerPhase::Direct) {
        return peer.pid == direct_broker_pid && peer.uid == real_uid;
    }
    if (peer.uid != real_uid && peer.uid != static_cast<uid_t>(0)) {
        return false;
    }

    struct stat peer_executable {};
    const std::string peer_path = "/proc/" + std::to_string(peer.pid) + "/exe";
    return stat(peer_path.c_str(), &peer_executable) == 0 &&
           static_cast<std::uint64_t>(peer_executable.st_dev) == executable_device &&
           static_cast<std::uint64_t>(peer_executable.st_ino) == executable_inode;
}

inline std::string GenerateSocketPath(const std::string& prefix) {
    static std::atomic<uint32_t> socket_counter{0};
    const auto pid = static_cast<unsigned long>(::getpid());
    for (int attempt = 0; attempt < kMaxProbeAttempts; ++attempt) {
        const auto suffix = socket_counter.fetch_add(1, std::memory_order_relaxed);
        std::string directory_template =
            "/tmp/encos_fdbroker_" + std::to_string(pid) + "_" + std::to_string(suffix) + "_XXXXXX";
        std::vector<char> directory(directory_template.begin(), directory_template.end());
        directory.push_back('\0');
        if (mkdtemp(directory.data()) == nullptr) {
            continue;
        }
        const auto path = std::filesystem::path(directory.data()) / (prefix + ".sock");
        if (path.string().size() < sizeof(((struct sockaddr_un*) nullptr)->sun_path)) {
            return path.string();
        }
        (void) rmdir(directory.data());
    }
    return std::string();
}

inline int SendFdWithHandshake(int control_fd, const Handshake& handshake, int transferred_fd) {
    struct msghdr msg {};
    struct iovec iov {};
    iov.iov_base = const_cast<Handshake*>(&handshake);
    iov.iov_len = sizeof(handshake);

    char ctrl[CMSG_SPACE(sizeof(int))];
    std::memset(ctrl, 0, sizeof(ctrl));

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (transferred_fd >= 0) {
        msg.msg_control = ctrl;
        msg.msg_controllen = sizeof(ctrl);
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        std::memcpy(CMSG_DATA(cmsg), &transferred_fd, sizeof(transferred_fd));
    }

    const ssize_t sent = sendmsg(control_fd, &msg, 0);
    return (sent == static_cast<ssize_t>(sizeof(handshake))) ? 0 : -1;
}

inline ReceivedBrokerHandshake ReceiveConnectedBrokerHandshake(int conn_fd, int timeout_ms) {
    ReceivedBrokerHandshake result;
    if (conn_fd < 0 || timeout_ms <= 0) {
        if (conn_fd >= 0) {
            close(conn_fd);
        }
        return result;
    }
    struct ucred peer_cred {};
    socklen_t peer_cred_len = sizeof(peer_cred);
    if (getsockopt(conn_fd, SOL_SOCKET, SO_PEERCRED, &peer_cred, &peer_cred_len) != 0) {
        close(conn_fd);
        return result;
    }
    result.peer.pid = peer_cred.pid;
    result.peer.uid = peer_cred.uid;

    struct timeval receive_timeout {};
    receive_timeout.tv_sec = static_cast<time_t>(timeout_ms / 1000);
    receive_timeout.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
    if (setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout)) !=
        0) {
        close(conn_fd);
        return result;
    }

    int received_fd = -1;
    struct msghdr msg {};
    struct iovec iov {};
    char ctrl[CMSG_SPACE(sizeof(int))];
    std::memset(ctrl, 0, sizeof(ctrl));

    iov.iov_base = &result.handshake;
    iov.iov_len = sizeof(result.handshake);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = ctrl;
    msg.msg_controllen = sizeof(ctrl);

    const ssize_t bytes = recvmsg(conn_fd, &msg, MSG_WAITALL | MSG_CMSG_CLOEXEC);
    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof(int))) {
            std::memcpy(&received_fd, CMSG_DATA(cmsg), sizeof(received_fd));
            break;
        }
    }

    close(conn_fd);
    if (bytes != static_cast<ssize_t>(sizeof(result.handshake)) ||
        (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0) {
        if (received_fd >= 0) {
            close(received_fd);
        }
        return result;
    }
    result.fd = received_fd;
    result.complete = true;
    return result;
}

inline ReceivedBrokerHandshake ReceiveBrokerHandshake(int listen_fd, int timeout_ms) {
    ReceivedBrokerHandshake result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    struct pollfd listen_poll {};
    listen_poll.fd = listen_fd;
    listen_poll.events = POLLIN;

    const int poll_ret = poll(&listen_poll, 1, timeout_ms);
    if (poll_ret <= 0) {
        return result;
    }

    const int conn_fd = accept(listen_fd, nullptr, nullptr);
    if (conn_fd < 0) {
        return result;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) {
        close(conn_fd);
        return result;
    }
    return ReceiveConnectedBrokerHandshake(conn_fd, static_cast<int>(remaining.count()));
}
#endif

}  // namespace fd_broker
}  // namespace encos
