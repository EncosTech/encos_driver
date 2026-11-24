// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "network_manager.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/poll.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace encos::ethernet {
namespace {
constexpr char kService[] = "org.freedesktop.NetworkManager";
constexpr char kManager[] = "/org/freedesktop/NetworkManager";
constexpr char kDevice[] = "org.freedesktop.NetworkManager.Device";
constexpr char kActive[] = "org.freedesktop.NetworkManager.Connection.Active";
constexpr uint32_t kNetworkManagerAsleep = 10;
constexpr uint64_t kRequestTimeout = 5'000'000;
constexpr uint64_t kAuthTimeout = 120'000'000;

using Message = std::unique_ptr<sd_bus_message, decltype(&sd_bus_message_unref)>;

struct BusError {
    sd_bus_error value = SD_BUS_ERROR_NULL;
    ~BusError() {
        sd_bus_error_free(&value);
    }
};

void Check(int result, const std::string& operation, const BusError* error = nullptr) {
    if (result < 0)
        throw std::runtime_error(
            operation + ": " +
            (error && error->value.message ? error->value.message : std::strerror(-result)));
}

Message Call(sd_bus* bus, const char* service, const char* path, const char* interface,
             const char* method, const char* input = nullptr, const char* argument = nullptr) {
    BusError error;
    sd_bus_message* reply = nullptr;
    const int result = sd_bus_call_method(bus, service, path, interface, method, &error.value,
                                          &reply, input, argument);
    Check(result, method, &error);
    return {reply, sd_bus_message_unref};
}

template <typename T>
T Property(sd_bus* bus, const std::string& path, const char* interface, const char* property,
           char type) {
    T value{};
    BusError error;
    const int result = sd_bus_get_property_trivial(bus, kService, path.c_str(), interface, property,
                                                   &error.value, type, &value);
    Check(result, property, &error);
    return value;
}

template <typename... Args>
void Append(sd_bus_message* message, const char* types, Args... args) {
    Check(sd_bus_message_append(message, types, args...), "Build NetworkManager profile");
}

void Open(sd_bus_message* message, char type, const char* contents) {
    Check(sd_bus_message_open_container(message, type, contents), "Open NetworkManager setting");
}

void Close(sd_bus_message* message) {
    Check(sd_bus_message_close_container(message), "Close NetworkManager setting");
}

void BeginSetting(sd_bus_message* message, const char* name) {
    Open(message, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}");
    Append(message, "s", name);
    Open(message, SD_BUS_TYPE_ARRAY, "{sv}");
}

template <typename T>
void Setting(sd_bus_message* message, const char* name, const char* type, T value) {
    Open(message, SD_BUS_TYPE_DICT_ENTRY, "sv");
    Append(message, "s", name);
    Open(message, SD_BUS_TYPE_VARIANT, type);
    Append(message, type, value);
    Close(message);
    Close(message);
}

void AddressSetting(sd_bus_message* message) {
    Open(message, SD_BUS_TYPE_DICT_ENTRY, "sv");
    Append(message, "s", "address-data");
    Open(message, SD_BUS_TYPE_VARIANT, "aa{sv}");
    Open(message, SD_BUS_TYPE_ARRAY, "a{sv}");
    Open(message, SD_BUS_TYPE_ARRAY, "{sv}");
    Setting(message, "address", "s", kMotorHostAddress);
    Setting(message, "prefix", "u", kMotorPrefix);
    for (int depth = 0; depth < 4; ++depth)
        Close(message);
}

void Profile(sd_bus_message* message, const std::string& interface, const std::string& user) {
    Open(message, SD_BUS_TYPE_ARRAY, "{sa{sv}}");
    BeginSetting(message, "connection");
    Setting(message, "id", "s", ("encos-" + interface).c_str());
    Setting(message, "interface-name", "s", interface.c_str());
    Setting(message, "type", "s", "802-3-ethernet");
    Setting(message, "autoconnect", "b", 0);
    if (!user.empty()) {
        Open(message, SD_BUS_TYPE_DICT_ENTRY, "sv");
        Append(message, "s", "permissions");
        Open(message, SD_BUS_TYPE_VARIANT, "as");
        Append(message, "as", 1, ("user:" + user + ":").c_str());
        Close(message);
        Close(message);
    }
    Close(message);
    Close(message);

    BeginSetting(message, "802-3-ethernet");
    Close(message);
    Close(message);

    BeginSetting(message, "ipv4");
    Setting(message, "method", "s", "manual");
    Setting(message, "may-fail", "b", 0);
    Setting(message, "never-default", "b", 1);
    AddressSetting(message);
    Close(message);
    Close(message);

    BeginSetting(message, "ipv6");
    Setting(message, "method", "s", "disabled");
    Close(message);
    Close(message);
    Close(message);
}

std::string DesktopUser() {
    if (std::getenv("SSH_CONNECTION") || std::getenv("SSH_TTY") || geteuid() == 0)
        return {};
    passwd entry{};
    passwd* result = nullptr;
    char buffer[16384];
    const int status = getpwuid_r(geteuid(), &entry, buffer, sizeof(buffer), &result);
    if (status || !result)
        throw std::runtime_error("Cannot determine the current user for NetworkManager");
    return result->pw_name;
}

bool NeedsAuthentication(sd_bus* bus, bool private_profile, const std::string& interface) {
    auto reply = Call(bus, kService, kManager, kService, "GetPermissions");
    constexpr std::string_view control = "org.freedesktop.NetworkManager.network-control";
    const std::string_view modify = private_profile
                                        ? "org.freedesktop.NetworkManager.settings.modify.own"
                                        : "org.freedesktop.NetworkManager.settings.modify.system";
    bool authentication = false;
    bool have_control = false, have_modify = false;
    Check(sd_bus_message_enter_container(reply.get(), SD_BUS_TYPE_ARRAY, "{ss}"),
          "Read NetworkManager permissions");
    int result;
    while ((result = sd_bus_message_enter_container(reply.get(), SD_BUS_TYPE_DICT_ENTRY, "ss")) >
           0) {
        const char *action = nullptr, *value = nullptr;
        Check(sd_bus_message_read(reply.get(), "ss", &action, &value), "Read permission result");
        Check(sd_bus_message_exit_container(reply.get()), "Read permission entry");
        if (action != control && action != modify)
            continue;
        if (std::strcmp(value, "no") == 0)
            throw std::runtime_error("NetworkManager denied " + std::string(action) + " on " +
                                     interface);
        have_control |= action == control;
        have_modify |= action == modify;
        authentication |= std::strcmp(value, "auth") == 0;
    }
    Check(result, "Read NetworkManager permissions");
    if (!have_control || !have_modify)
        throw std::runtime_error("NetworkManager did not report permissions for " + interface);
    return authentication;
}

class TerminalAgent {
public:
    ~TerminalAgent() {
        if (child_ > 0) {
            kill(child_, SIGTERM);
            while (waitpid(child_, nullptr, 0) < 0 && errno == EINTR) {}
        }
    }

    void Start() {
        const int terminal = open("/dev/tty", O_RDWR | O_CLOEXEC);
        if (terminal < 0)
            throw std::runtime_error(
                "NetworkManager authentication needs an SSH terminal; reconnect using ssh -t");
        close(terminal);
        std::ifstream stat("/proc/self/stat");
        std::string process_stat;
        std::getline(stat, process_stat);
        const auto end_name = process_stat.rfind(')');
        if (end_name == std::string::npos)
            throw std::runtime_error("Cannot read the current process identity for Polkit");
        std::istringstream fields(process_stat.substr(end_name + 1));
        std::string start_time;
        for (int field = 3; field <= 22; ++field)
            if (!(fields >> start_time))
                throw std::runtime_error("Cannot read the current process start time for Polkit");
        const auto subject = std::to_string(getpid()) + "," + start_time;
        int ready[2];
        if (pipe2(ready, O_CLOEXEC) < 0)
            Check(-errno, "Create pkttyagent notification pipe");
        int notify_fd = 3;
        while (notify_fd == ready[0] || notify_fd == ready[1])
            ++notify_fd;
        const auto fd = std::to_string(notify_fd);
        char* const args[]{const_cast<char*>("pkttyagent"),    const_cast<char*>("--process"),
                           const_cast<char*>(subject.c_str()), const_cast<char*>("--notify-fd"),
                           const_cast<char*>(fd.c_str()),      nullptr};
        posix_spawn_file_actions_t actions;
        int status = posix_spawn_file_actions_init(&actions);
        if (status == 0) {
            status = posix_spawn_file_actions_adddup2(&actions, ready[1], notify_fd);
            if (status == 0)
                status =
                    posix_spawn(&child_, "/usr/bin/pkttyagent", &actions, nullptr, args, environ);
            posix_spawn_file_actions_destroy(&actions);
        }
        close(ready[1]);
        pollfd event{ready[0], POLLIN, 0};
        const int registered = status == 0 ? poll(&event, 1, 5000) : -1;
        close(ready[0]);
        if (status != 0)
            Check(-status, "Start pkttyagent for NetworkManager authentication");
        if (registered != 1 || (event.revents & POLLHUP) == 0)
            throw std::runtime_error("Timed out registering the SSH Polkit authentication agent");
        const auto exited = waitpid(child_, nullptr, WNOHANG);
        if (exited != 0) {
            child_ = -1;
            throw std::runtime_error("Could not register the SSH Polkit authentication agent");
        }
    }

private:
    pid_t child_ = -1;
};
}  // namespace

void NetworkManager::CloseBus::operator()(sd_bus* bus) const {
    sd_bus_close(bus);
    sd_bus_unref(bus);
}

NetworkManager::NetworkManager(const std::string& interface) {
    sd_bus* bus = nullptr;
    const int opened = sd_bus_open_system(&bus);
    if (opened == -ENOENT)
        return;
    Check(opened, "Open the system D-Bus");
    bus_.reset(bus);
    auto owner = Call(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                      "NameHasOwner", "s", kService);
    int present = 0;
    Check(sd_bus_message_read(owner.get(), "b", &present), "Detect NetworkManager");
    if (!present)
        return;
    if (!Property<int>(bus, kManager, kService, "NetworkingEnabled", 'b'))
        throw std::runtime_error("NetworkManager networking is disabled on " + interface);
    if (Property<uint32_t>(bus, kManager, kService, "State", 'u') == kNetworkManagerAsleep)
        throw std::runtime_error("NetworkManager is asleep on " + interface);

    BusError error;
    sd_bus_message* raw = nullptr;
    const int result = sd_bus_call_method(bus, kService, kManager, kService, "GetDeviceByIpIface",
                                          &error.value, &raw, "s", interface.c_str());
    if (sd_bus_error_has_name(&error.value, "org.freedesktop.NetworkManager.UnknownDevice"))
        throw std::runtime_error("NetworkManager has not reported device " + interface);
    Check(result, "Find NetworkManager device " + interface, &error);
    Message reply(raw, sd_bus_message_unref);
    const char* path = nullptr;
    Check(sd_bus_message_read(reply.get(), "o", &path), "Read NetworkManager device");
    if (Property<int>(bus, path, kDevice, "Managed", 'b')) {
        if (Property<uint32_t>(bus, path, kDevice, "DeviceType", 'u') != 1)
            throw std::runtime_error("NetworkManager device is not wired Ethernet: " + interface);
        device_ = path;
    }
}

NetworkIpv4Method NetworkManager::Ipv4Method() const {
    BusError error;
    sd_bus_message* raw = nullptr;
    const int result =
        sd_bus_call_method(bus_.get(), kService, device_.c_str(), kDevice, "GetAppliedConnection",
                           &error.value, &raw, "u", uint32_t{0});
    if (sd_bus_error_has_name(&error.value, "org.freedesktop.NetworkManager.Device.NotActive"))
        return NetworkIpv4Method::Unavailable;
    Check(result, "Read the applied NetworkManager IPv4 configuration", &error);
    Message reply(raw, sd_bus_message_unref);
    auto* message = reply.get();
    Check(sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sa{sv}}"),
          "Read applied connection");
    int next;
    while ((next = sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}")) > 0) {
        const char* name = nullptr;
        Check(sd_bus_message_read(message, "s", &name), "Read applied setting name");
        if (std::strcmp(name, "ipv4") == 0) {
            Check(sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sv}"),
                  "Read applied IPv4 settings");
            int field;
            while ((field = sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv")) >
                   0) {
                const char* key = nullptr;
                Check(sd_bus_message_read(message, "s", &key), "Read applied IPv4 setting name");
                if (std::strcmp(key, "method") == 0) {
                    const char* mode = nullptr;
                    Check(sd_bus_message_read(message, "v", "s", &mode), "Read applied IPv4 mode");
                    if (std::strcmp(mode, "manual") == 0)
                        return NetworkIpv4Method::Manual;
                    if (std::strcmp(mode, "auto") == 0)
                        return NetworkIpv4Method::Dhcp;
                    return NetworkIpv4Method::Other;
                }
                Check(sd_bus_message_skip(message, "v"), "Skip applied IPv4 setting");
                Check(sd_bus_message_exit_container(message), "Read applied IPv4 settings");
            }
            Check(field, "Read applied IPv4 settings");
            return NetworkIpv4Method::Unavailable;
        }
        Check(sd_bus_message_skip(message, "a{sv}"), "Skip applied setting");
        Check(sd_bus_message_exit_container(message), "Read applied connection");
    }
    Check(next, "Read applied connection");
    return NetworkIpv4Method::Unavailable;
}

bool NetworkManager::AvailableForActivation(NetworkIpv4Method method) const {
    const auto state = Property<uint32_t>(bus_.get(), device_, kDevice, "State", 'u');
    // Disconnected or failed devices can be activated. An existing activation must use DHCP.
    return state == 30 || state == 120 ||
           ((state == 40 || state == 50 || state == 70) && method == NetworkIpv4Method::Dhcp);
}

void NetworkManager::Activate(const std::string& interface,
                              const std::function<void()>& check_takeover) {
    const auto user = DesktopUser();
    const bool auth = geteuid() != 0 && NeedsAuthentication(bus_.get(), !user.empty(), interface);
    const bool ssh = std::getenv("SSH_CONNECTION") || std::getenv("SSH_TTY");
    TerminalAgent agent;
    if (auth && ssh)
        agent.Start();

    auto* bus = bus_.get();
    sd_bus_message* raw = nullptr;
    Check(sd_bus_message_new_method_call(bus, &raw, kService, kManager, kService,
                                         "AddAndActivateConnection2"),
          "Create NetworkManager activation request");
    Message request(raw, sd_bus_message_unref);
    Profile(raw, interface, user);
    Append(raw, "oo", device_.c_str(), "/");
    Open(raw, SD_BUS_TYPE_ARRAY, "{sv}");
    Setting(raw, "persist", "s", "volatile");
    Setting(raw, "bind-activation", "s", "dbus-client");
    Close(raw);

    if (!Property<int>(bus, device_, kDevice, "Autoconnect", 'b')) {
        // 先设置用户级限制，保留显式激活会清除的手动断开状态。
        BusError error;
        const int result = sd_bus_set_property(bus, kService, device_.c_str(), kDevice,
                                               "Autoconnect", &error.value, "b", 0);
        Check(result, "Preserve disabled NetworkManager autoconnect on " + interface, &error);
    }
    if (!AvailableForActivation(Ipv4Method()))
        throw std::runtime_error("NetworkManager device changed before activation on " + interface);
    check_takeover();

    // Recheck the interface while Polkit may be waiting for the user. A DHCP
    // lease can arrive in that window. Closing the bus withdraws the activation.
    Message reply(nullptr, sd_bus_message_unref);
    sd_bus_slot* raw_slot = nullptr;
    const auto on_reply = [](sd_bus_message* message, void* userdata, sd_bus_error*) {
        static_cast<Message*>(userdata)->reset(sd_bus_message_ref(message));
        return 1;
    };
    const auto operation = "Activate temporary NetworkManager connection on " + interface;
    Check(sd_bus_call_async(bus, &raw_slot, raw, on_reply, &reply,
                            auth ? kAuthTimeout : kRequestTimeout),
          operation);
    const std::unique_ptr<sd_bus_slot, decltype(&sd_bus_slot_unref)> slot(raw_slot,
                                                                          sd_bus_slot_unref);
    while (!reply) {
        check_takeover();
        const int processed = sd_bus_process(bus, nullptr);
        Check(processed, operation);
        if (processed == 0 && !reply)
            Check(sd_bus_wait(bus, 200'000), operation);
    }
    check_takeover();
    if (const auto* error = sd_bus_message_get_error(reply.get()))
        throw std::runtime_error(operation + ": " +
                                 (error->message ? error->message : error->name));
    const char* profile = nullptr;
    const char* active = nullptr;
    Check(sd_bus_message_read(reply.get(), "oo", &profile, &active), "Read active connection path");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        const auto state = Property<uint32_t>(bus, active, kActive, "State", 'u');
        if (state == 2)
            return;
        if (state >= 3)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("NetworkManager connection did not activate on " + interface);
}
}  // namespace encos::ethernet
