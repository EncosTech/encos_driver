#include "plugins/export.h"
#include "plugins/fake/fake_adapter.h"

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(const char* interface_name,
                                                            const char* logger_name,
                                                            int log_level) {
    return new encos::FakeAdapter(interface_name, logger_name, encos::LogLevelFromInt(log_level));
}
