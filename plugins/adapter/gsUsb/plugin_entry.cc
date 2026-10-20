// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "adapter/adapter_plugin.h"
#include "export.h"
#include "gs_usb_adapter.h"

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(const char* interface_name,
                                                            const char* logger_name,
                                                            int log_level) {
    return encos::GsUsbAdapter::Create(interface_name, logger_name,
                                       encos::LogLevelFromInt(log_level));
}

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapterV2(const char* interface_name,
                                                              const char* logger_name,
                                                              int log_level,
                                                              const char* extend_json) {
    return encos::GsUsbAdapter::CreateWithExtend(interface_name, logger_name,
                                                 encos::LogLevelFromInt(log_level),
                                                 extend_json != nullptr ? extend_json : "");
}

extern "C" ENCOS_PLUGIN_API void EnumerateInterfaces(encos::AdapterInterfaceEmit emit,
                                                     void* context) {
    encos::EnumerateGsUsbInterfaces(emit, context);
}
