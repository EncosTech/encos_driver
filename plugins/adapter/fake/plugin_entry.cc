// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "plugins/adapter/fake/fake_adapter.h"
#include "plugins/export.h"

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(const char* interface_name,
                                                            const char* logger_name,
                                                            int log_level) {
    return new encos::FakeAdapter(interface_name, logger_name, encos::LogLevelFromInt(log_level));
}

extern "C" ENCOS_PLUGIN_API void EnumerateInterfaces(encos::AdapterInterfaceEmit emit,
                                                     void* context) {
    encos::EnumerateFakeInterfaces(emit, context);
}
