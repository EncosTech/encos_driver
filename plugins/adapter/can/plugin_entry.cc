// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "adapter/adapter_plugin.h"
#include "can_adapter.h"
#include "export.h"

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(const char* interface_name,
                                                            const char* logger_name,
                                                            int log_level) {
    return encos::CanAdapter::Create(interface_name, logger_name,
                                     encos::LogLevelFromInt(log_level));
}

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapterV2(const char* interface_name,
                                                              const char* logger_name,
                                                              int log_level,
                                                              const char* extend_json) {
    return encos::CanAdapter::CreateWithExtend(interface_name, logger_name,
                                               encos::LogLevelFromInt(log_level),
                                               extend_json != nullptr ? extend_json : "");
}

extern "C" ENCOS_PLUGIN_API void EnumerateInterfaces(encos::AdapterInterfaceEmit emit,
                                                     void* context) {
    encos::EnumerateCanInterfaces(emit, context);
}
