// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "adapter/adapter_plugin.h"
#include "ethernet_adapter.h"
#include "export.h"

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(const char* interface_name,
                                                            const char* logger_name, int level) {
    return encos::CreateEthernetAdapterStatic(interface_name, logger_name,
                                              encos::LogLevelFromInt(level));
}

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapterV2(const char* interface_name,
                                                              const char* logger_name, int level,
                                                              const char* extend_json) {
    return encos::CreateEthernetAdapterStaticWithExtend(interface_name, logger_name,
                                                        encos::LogLevelFromInt(level),
                                                        extend_json != nullptr ? extend_json : "");
}

extern "C" ENCOS_PLUGIN_API void EnumerateInterfaces(encos::AdapterInterfaceEmit emit,
                                                     void* context) {
    encos::EnumerateEthernetInterfaces(emit, context);
}
