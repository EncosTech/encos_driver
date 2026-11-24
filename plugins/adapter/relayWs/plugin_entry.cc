// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "export.h"
#include "relayWs/relay_ws_adapter.h"

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(const char* interface_name,
                                                            const char* logger_name,
                                                            int log_level) {
    return encos::CreateRelayWsAdapterStatic(interface_name, logger_name,
                                             encos::LogLevelFromInt(log_level));
}

extern "C" ENCOS_PLUGIN_API void EnumerateInterfaces(encos::AdapterInterfaceEmit emit,
                                                     void* context) {
    encos::EnumerateRelayWsInterfaces(emit, context);
}

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapterV2(const char* interface_name,
                                                              const char* logger_name,
                                                              int log_level,
                                                              const char* extend_json) {
    return encos::CreateRelayWsAdapterStaticWithExtend(interface_name, logger_name,
                                                       encos::LogLevelFromInt(log_level),
                                                       extend_json != nullptr ? extend_json : "");
}
