#include "plugins/export.h"
#include "relayWs/relay_ws_adapter.h"

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(const char* interface_name,
                                                            const char* logger_name,
                                                            int log_level) {
    return encos::CreateRelayWsAdapterStatic(interface_name, logger_name,
                                             encos::LogLevelFromInt(log_level));
}
