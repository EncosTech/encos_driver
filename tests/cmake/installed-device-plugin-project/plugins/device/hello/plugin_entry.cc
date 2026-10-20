// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "device/device_plugin.h"
#include "hello.h"

namespace hello {
const encos::DeviceDriverDescriptorV1* MakeHelloDriver();
}  // namespace hello

extern "C" const encos::DeviceDriverDescriptorV1* MakeDeviceDriver() {
    return hello::MakeHelloDriver();
}
