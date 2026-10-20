// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <stdexcept>
#include <string>

#include "encos/encos_driver.h"
#include "platform/os.h"

int main() {
    const auto types = encos::GetAvailableAdapterTypes();
    if (std::find(types.begin(), types.end(), "Ethercat") == types.end()) {
        return 1;
    }
    auto expected = encos::platform::GetWiredInterfaceNames();
    std::sort(expected.begin(), expected.end());
    if (encos::GetAvailableInterface("Ethercat") != expected) {
        return 2;
    }
    try {
        (void) encos::MakeAdapter("Ethercat", "__encos_missing_interface__");
        return 3;
    } catch (const std::invalid_argument& error) {
        if (std::string(error.what()).find("Unknown EtherCAT interface") == std::string::npos) {
            return 4;
        }
    }
    return 0;
}
