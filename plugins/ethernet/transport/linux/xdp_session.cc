#include "xdp_session.h"

#include <arpa/inet.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <linux/if_link.h>
#include <net/if.h>
#include <net/route.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <xdp/xsk.h>

#include "utils/fdBroker/file_capabilities.h"

namespace encos::ethernet {
namespace {
void Require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(std::string(message) + ": " + std::strerror(errno));
}
}  // namespace
void RequireXdpProcessCapabilities() {
    cap_t caps = cap_get_proc();
    const bool present = fd_broker::HasCapabilities(caps, {CAP_NET_RAW, CAP_NET_ADMIN, CAP_BPF});
    if (caps)
        cap_free(caps);
    if (!present)
        throw std::runtime_error(
            "Static Ethernet requires effective cap_net_raw,cap_net_admin,cap_bpf on the "
            "final executable; authorize it with sudo setcap "
            "cap_net_raw,cap_net_admin,cap_bpf+ep <Executable>");
}
void ConfigureXdpInterfaceDirect(const char* name) {
    Require(
        std::strlen(name) > 0 && std::strlen(name) < IFNAMSIZ && std::strchr(name, ':') == nullptr,
        "Invalid network interface name");
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    Require(fd >= 0, "Open network configuration socket");
    try {
        ifreq request{};
        std::strncpy(request.ifr_name, name, IFNAMSIZ - 1);
        Require(ioctl(fd, SIOCGIFFLAGS, &request) == 0, "Read interface flags");
        Require(!(request.ifr_flags & IFF_LOOPBACK), "Cannot configure loopback");
        const auto flags = request.ifr_flags;
        auto* address = reinterpret_cast<sockaddr_in*>(&request.ifr_addr);
        address->sin_family = AF_INET;
        inet_pton(AF_INET, "192.168.100.254", &address->sin_addr);
        Require(ioctl(fd, SIOCSIFADDR, &request) == 0, "Set host IP (requires CAP_NET_ADMIN)");
        address->sin_family = AF_INET;
        inet_pton(AF_INET, "255.255.255.0", &address->sin_addr);
        Require(ioctl(fd, SIOCSIFNETMASK, &request) == 0, "Set host netmask");
        address->sin_family = AF_INET;
        inet_pton(AF_INET, "192.168.100.255", &address->sin_addr);
        Require(ioctl(fd, SIOCSIFBRDADDR, &request) == 0, "Set broadcast address");
        request.ifr_flags = static_cast<short>(flags | IFF_UP);
        Require(ioctl(fd, SIOCSIFFLAGS, &request) == 0, "Enable network interface");
        rtentry route{};
        auto* destination = reinterpret_cast<sockaddr_in*>(&route.rt_dst);
        auto* mask = reinterpret_cast<sockaddr_in*>(&route.rt_genmask);
        auto* gateway = reinterpret_cast<sockaddr_in*>(&route.rt_gateway);
        destination->sin_family = mask->sin_family = gateway->sin_family = AF_INET;
        inet_pton(AF_INET, "192.168.100.0", &destination->sin_addr);
        inet_pton(AF_INET, "255.255.255.0", &mask->sin_addr);
        route.rt_flags = RTF_UP;
        route.rt_dev = const_cast<char*>(name);
        Require(ioctl(fd, SIOCADDRT, &route) == 0 || errno == EEXIST, "Add motor subnet route");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;) {
            Require(ioctl(fd, SIOCGIFFLAGS, &request) == 0, "Read link state");
            if (request.ifr_flags & IFF_RUNNING)
                break;
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Ethernet link did not become ready within 10 seconds");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } catch (...) {
        close(fd);
        throw;
    }
    close(fd);
}
struct XdpSession::Impl {
    int memory_fd = -1;
    void* memory = MAP_FAILED;
    xsk_umem* umem = nullptr;
    xsk_socket* socket = nullptr;
    xsk_ring_prod fill{};
    xsk_ring_cons completion{};
    xsk_ring_cons rx{};
    xsk_ring_prod tx{};
    bpf_object* object = nullptr;
    unsigned interface_index = 0;
    int program_fd = -1;
    bool attached = false;
    ~Impl() {
        if (attached) {
            bpf_xdp_attach_opts options{};
            options.sz = sizeof(options);
            options.old_prog_fd = program_fd;
            bpf_xdp_detach(static_cast<int>(interface_index), XDP_FLAGS_SKB_MODE, &options);
        }
        if (socket)
            xsk_socket__delete(socket);
        if (umem)
            xsk_umem__delete(umem);
        if (memory != MAP_FAILED)
            munmap(memory, ENCOS_XDP_UMEM_SIZE);
        if (memory_fd >= 0)
            close(memory_fd);
        if (object)
            bpf_object__close(object);
    }
    void Open(const encos_xdp_request& request, const std::string& path, const unsigned char* data,
              std::size_t size) {
        Require(std::memchr(request.interface_name, 0, sizeof(request.interface_name)) != nullptr &&
                    request.filter.queue < 64,
                "Invalid AF_XDP configuration");
        interface_index = if_nametoindex(request.interface_name);
        Require(interface_index != 0, "Cannot resolve network interface");
        memory_fd = memfd_create("encos-xdp-umem", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        Require(memory_fd >= 0, "memfd_create");
        Require(ftruncate(memory_fd, ENCOS_XDP_UMEM_SIZE) == 0, "ftruncate UMEM");
        memory =
            mmap(nullptr, ENCOS_XDP_UMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, memory_fd, 0);
        Require(memory != MAP_FAILED, "mmap UMEM");
        xsk_umem_config umem_config{};
        umem_config.fill_size = ENCOS_XDP_RX_SIZE;
        umem_config.comp_size = ENCOS_XDP_TX_SIZE;
        umem_config.frame_size = ENCOS_XDP_FRAME_SIZE;
        int result =
            xsk_umem__create(&umem, memory, ENCOS_XDP_UMEM_SIZE, &fill, &completion, &umem_config);
        if (result) {
            errno = -result;
            Require(false, "AF_XDP UMEM (check cap_net_raw and memlock limit)");
        }
        xsk_socket_config socket_config{};
        socket_config.rx_size = ENCOS_XDP_RX_SIZE;
        socket_config.tx_size = ENCOS_XDP_TX_SIZE;
        socket_config.libbpf_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD;
        socket_config.xdp_flags = XDP_FLAGS_SKB_MODE;
        socket_config.bind_flags = XDP_COPY | XDP_USE_NEED_WAKEUP;
        result = xsk_socket__create(&socket, request.interface_name, request.filter.queue, umem,
                                    &rx, &tx, &socket_config);
        if (result) {
            errno = -result;
            Require(false, "AF_XDP socket bind in generic/copy mode");
        }
        object = data ? bpf_object__open_mem(data, size, nullptr)
                      : bpf_object__open_file(path.c_str(), nullptr);
        const auto error = libbpf_get_error(object);
        if (error) {
            object = nullptr;
            errno = static_cast<int>(-error);
            Require(false, "Open XDP filter object");
        }
        Require(bpf_object__load(object) == 0,
                "Load XDP filter (requires cap_bpf and cap_net_admin)");
        const int map_fd = bpf_object__find_map_fd_by_name(object, "sockets");
        const int config_fd = bpf_object__find_map_fd_by_name(object, "filters");
        const uint32_t zero = 0;
        const int xsk_fd = xsk_socket__fd(socket);
        Require(map_fd >= 0 && config_fd >= 0, "Find XDP maps");
        Require(bpf_map_update_elem(config_fd, &zero, &request.filter, BPF_ANY) == 0,
                "Configure XDP tuple filter");
        Require(bpf_map_update_elem(map_fd, &request.filter.queue, &xsk_fd, BPF_ANY) == 0,
                "Configure XSK map");
        auto* program = bpf_object__find_program_by_name(object, "ethernet_redirect");
        Require(program != nullptr, "Find XDP program");
        program_fd = bpf_program__fd(program);
        Require(bpf_xdp_attach(static_cast<int>(interface_index), program_fd,
                               XDP_FLAGS_SKB_MODE | XDP_FLAGS_UPDATE_IF_NOEXIST, nullptr) == 0,
                "Attach XDP filter (existing XDP program is not replaced)");
        attached = true;
    }
};

XdpSession::XdpSession() : impl_(std::make_unique<Impl>()) {}
XdpSession::~XdpSession() = default;
void XdpSession::OpenFile(const encos_xdp_request& request, const std::string& path) {
    if (impl_->interface_index)
        throw std::logic_error("XDP session already initialized");
    impl_->Open(request, path, nullptr, 0);
}
void XdpSession::OpenMemory(const encos_xdp_request& request, const unsigned char* data,
                            std::size_t size) {
    if (!data || !size)
        throw std::invalid_argument("Empty XDP filter object");
    if (impl_->interface_index)
        throw std::logic_error("XDP session already initialized");
    impl_->Open(request, {}, data, size);
}
int XdpSession::SocketFd() const {
    return impl_->socket ? xsk_socket__fd(impl_->socket) : -1;
}
int XdpSession::MemoryFd() const {
    return impl_->memory_fd;
}
}  // namespace encos::ethernet
