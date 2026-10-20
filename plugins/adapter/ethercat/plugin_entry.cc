// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adapter/adapter_plugin.h"
#include "ethercat_adapter.h"
#include "export.h"

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(const char* interface_name,
                                                            const char* logger_name,
                                                            int log_level) {
    return encos::EthercatAdapter::Create(interface_name, logger_name,
                                          encos::LogLevelFromInt(log_level));
}

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapterV2(const char* interface_name,
                                                              const char* logger_name, int level,
                                                              const char* extend_json) {
    return encos::CreateEthercatAdapterStaticWithExtend(interface_name, logger_name,
                                                        encos::LogLevelFromInt(level),
                                                        extend_json != nullptr ? extend_json : "");
}

extern "C" ENCOS_PLUGIN_API void EnumerateInterfaces(encos::AdapterInterfaceEmit emit,
                                                     void* context) {
    encos::EnumerateEthercatInterfaces(emit, context);
}
