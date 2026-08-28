#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "platform/log.h"

namespace encos {
namespace fd_broker {

class FdBrokerClient {
public:
    static int RequestFd(const std::string& broker_executable, const std::string& interface_name,
                         const std::string& socket_prefix, LoggerPtr logger,
                         std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
};

}  // namespace fd_broker
}  // namespace encos
