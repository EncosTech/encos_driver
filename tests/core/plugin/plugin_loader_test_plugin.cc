#include <memory>
#include <string>
#include <unordered_map>

#include "adapter/base_adapter.h"
#include "bus/bus.h"
#include "plugins/export.h"

namespace {

class PluginCacheTestAdapter : public encos::BaseAdapter {
public:
    PluginCacheTestAdapter(const std::string& interface_name, const std::string& logger_name,
                           encos::LogLevel log_level)
        : BaseAdapter(interface_name, logger_name, log_level) {}

    std::unordered_map<int, encos::Bus*> GetBuses() override {
        return {};
    }

    bool Ok() override {
        return true;
    }

protected:
    void Send(const encos::MotorMessage& message) override {
        (void) message;
    }
};

}  // namespace

extern "C" ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(const char* interface_name,
                                                            const char* logger_name,
                                                            int log_level) {
    if (std::string(interface_name) == "plugin-null-result") {
        return nullptr;
    }
    if (std::string(interface_name) == "plugin-identity-mismatch") {
        return new PluginCacheTestAdapter("different-interface", logger_name,
                                          encos::LogLevelFromInt(log_level));
    }
    return new PluginCacheTestAdapter(interface_name, logger_name,
                                      encos::LogLevelFromInt(log_level));
}
