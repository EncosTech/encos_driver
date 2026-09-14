#include "ethernet_adapter.h"
#include "export.h"
#include "platform/os.h"
extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(const char* interface_name,
                                                            const char* logger_name, int level) {
    return encos::CreateEthernetAdapterStatic(interface_name, logger_name,
                                              encos::LogLevelFromInt(level));
}

extern "C" ENCOS_PLUGIN_API void EnumerateInterfaces(void (*emit)(const char*, void*),
                                                     void* context) {
    for (const auto& endpoint : encos::platform::GetWiredInterfaceNames())
        emit(endpoint.c_str(), context);
}
