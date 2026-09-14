#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "xdp_session.h"

namespace {
void Require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(std::string(message) + ": " + std::strerror(errno));
}
void Reply(int control, const encos_xdp_reply& reply, const std::array<int, 2>* fds) {
    iovec io{const_cast<encos_xdp_reply*>(&reply), sizeof(reply)};
    msghdr message{};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    alignas(cmsghdr) std::array<char, CMSG_SPACE(sizeof(int) * 2)> ancillary{};
    if (fds) {
        message.msg_control = ancillary.data();
        message.msg_controllen = ancillary.size();
        auto* c = CMSG_FIRSTHDR(&message);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int) * 2);
        std::memcpy(CMSG_DATA(c), fds->data(), sizeof(int) * 2);
    }
    Require(sendmsg(control, &message, MSG_NOSIGNAL) == sizeof(reply), "Send bootstrap reply");
}
}  // namespace
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--configure-interface") {
        try {
            encos::ethernet::ConfigureXdpInterfaceDirect(argv[2]);
            return 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "%s\n", error.what());
            return 1;
        }
    }
    if (argc != 3 || std::string(argv[1]) != "--control-fd" || std::string(argv[2]) != "3")
        return 2;
    constexpr int control = 3;
    try {
        int type = 0;
        socklen_t type_size = sizeof(type);
        Require(getsockopt(control, SOL_SOCKET, SO_TYPE, &type, &type_size) == 0 &&
                    type == SOCK_SEQPACKET,
                "Expected private socketpair");
        encos_xdp_request request{};
        Require(recv(control, &request, sizeof(request), MSG_TRUNC) == sizeof(request),
                "Receive XDP configuration");
        encos::ethernet::XdpSession session;
        const auto path =
            std::filesystem::canonical("/proc/self/exe").parent_path() / "ethernet_xdp.bpf.o";
        session.OpenFile(request, path.string());
        const std::array<int, 2> fds{session.SocketFd(), session.MemoryFd()};
        Reply(control, encos_xdp_reply{}, &fds);
        char command = 0;
        while (recv(control, &command, 1, 0) < 0 && errno == EINTR) {}
        return 0;
    } catch (const std::exception& error) {
        encos_xdp_reply reply{};
        reply.error = errno ? errno : EIO;
        std::snprintf(reply.message, sizeof(reply.message), "%s", error.what());
        try {
            Reply(control, reply, nullptr);
        } catch (...) {}
        return 1;
    }
}
