#pragma once

#include <memory>
#include <string>

#include "platform/log.h"

namespace encos::can {

bool HasRequiredRuntimeCapabilities(LoggerPtr logger);
bool InitializeCanInterface(const std::string& ifname, LoggerPtr logger);
bool EnsureCanInterfaceReady(const std::string& ifname, LoggerPtr logger);
int CreateConfiguredCanSocket(const std::string& ifname);

}  // namespace encos::can
