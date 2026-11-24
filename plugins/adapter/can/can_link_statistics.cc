// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "can_link_statistics.h"

#include <array>
#include <cstring>
#include <linux/can/netlink.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace encos::can {

namespace {

template <typename F>
void Attributes(const void* data, std::size_t size, F callback) {
    auto* attribute = static_cast<const rtattr*>(data);
    int length = static_cast<int>(size);

    while (RTA_OK(attribute, length)) {
        callback(attribute->rta_type & NLA_TYPE_MASK, RTA_DATA(attribute),
                 static_cast<std::size_t>(RTA_PAYLOAD(attribute)));
        attribute = RTA_NEXT(attribute, length);
    }
}

template <typename T>
bool Copy(const void* data, std::size_t size, T& value) {
    if (size < sizeof(T))
        return false;

    std::memcpy(&value, data, sizeof(T));
    return true;
}

void CanAttributes(BusStatistics& bus, const void* data, std::size_t size) {
    Attributes(data, size, [&](unsigned type, const void* value, std::size_t length) {
        can_berr_counter counters{};

        if (type == IFLA_CAN_BERR_COUNTER && Copy(value, length, counters)) {
            bus.error_counters =
                CanErrorCounters{counters.txerr, counters.rxerr, ErrorCounterKind::Current};
        }
    });
}

void LinkInfo(BusStatistics& bus, const void* data, std::size_t size) {
    bool is_can = false;

    Attributes(data, size, [&](unsigned type, const void* value, std::size_t length) {
        if (type == IFLA_INFO_KIND && length >= 4 && std::memcmp(value, "can\0", 4) == 0)
            is_can = true;
    });

    if (!is_can)
        return;

    Attributes(data, size, [&](unsigned type, const void* value, std::size_t length) {
        if (type == IFLA_INFO_DATA)
            CanAttributes(bus, value, length);
    });
}

}  // namespace

std::optional<BusStatistics> DecodeLinkStatistics(const void* data, std::size_t size, int index) {
    const auto* header = static_cast<const nlmsghdr*>(data);
    int length = static_cast<int>(size);

    for (; NLMSG_OK(header, length); header = NLMSG_NEXT(header, length)) {
        if (header->nlmsg_type != RTM_NEWLINK ||
            header->nlmsg_len < NLMSG_LENGTH(sizeof(ifinfomsg)))
            continue;

        const auto* info = static_cast<const ifinfomsg*>(NLMSG_DATA(header));
        if (info->ifi_index != index)
            continue;

        BusStatistics bus;
        bus.bus_id = 0;
        Attributes(IFLA_RTA(info), IFLA_PAYLOAD(header),
                   [&](unsigned type, const void* value, std::size_t count) {
                       if (type == IFLA_LINKINFO)
                           LinkInfo(bus, value, count);
                   });

        return bus;
    }

    return std::nullopt;
}

std::optional<BusStatistics> ReadLinkStatistics(const std::string& interface_name) {
    const unsigned index = if_nametoindex(interface_name.c_str());
    if (index == 0)
        return std::nullopt;

    const int descriptor =
        socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
    if (descriptor < 0)
        return std::nullopt;

    struct Request {
        nlmsghdr header;
        ifinfomsg info;
    } request{};
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(ifinfomsg));
    request.header.nlmsg_type = RTM_GETLINK;
    request.header.nlmsg_flags = NLM_F_REQUEST;
    request.header.nlmsg_seq = 1;
    request.info.ifi_family = AF_UNSPEC;
    request.info.ifi_index = static_cast<int>(index);

    sockaddr_nl kernel{};
    kernel.nl_family = AF_NETLINK;

    std::optional<BusStatistics> result;

    if (sendto(descriptor, &request, request.header.nlmsg_len, MSG_DONTWAIT,
               reinterpret_cast<sockaddr*>(&kernel), sizeof(kernel)) >= 0) {
        pollfd pending{descriptor, POLLIN, 0};
        if (poll(&pending, 1, 50) > 0 && (pending.revents & POLLIN)) {
            alignas(nlmsghdr) std::array<std::uint8_t, 16384> buffer{};
            const auto received =
                recv(descriptor, buffer.data(), buffer.size(), MSG_DONTWAIT | MSG_TRUNC);
            if (received > 0 && static_cast<std::size_t>(received) <= buffer.size())
                result = DecodeLinkStatistics(buffer.data(), static_cast<std::size_t>(received),
                                              request.info.ifi_index);
        }
    }

    close(descriptor);
    return result;
}

}  // namespace encos::can
