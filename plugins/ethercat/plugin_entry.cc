// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ethercat_adapter.h"

extern "C" encos::BaseAdapter* MakeAdapter(const char* interface_name, const char* logger_name,
                                           int log_level) {
    return encos::EthercatAdapter::Create(interface_name, logger_name,
                                          encos::LogLevelFromInt(log_level));
}
