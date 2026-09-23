// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <limits>
#include <string>

#include "network_configuration.h"
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    const std::string input = argv[1];
    if (input.empty() || input.size() > 10 ||
        input.find_first_not_of("0123456789") != std::string::npos)
        return 2;
    unsigned long long value = 0;
    try {
        value = std::stoull(input);
    } catch (...) {
        return 2;
    }
    if (!value || value > std::numeric_limits<unsigned>::max())
        return 2;
    return encos::ethernet::ConfigureWindowsInterface(static_cast<unsigned>(value));
}
