// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "test_device.h"

extern "C" const encos::DeviceDriverDescriptorV1* MakeDeviceDriver() {
    return encos::MakeTestDeviceDriver();
}
