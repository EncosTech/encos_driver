#include <atomic>
#include <cstdlib>
#include <string>
#include <unordered_map>

#include "adapter/base_adapter.h"
#include "driver_manager_test_access.h"
#include "encos/driver_manager.h"

namespace encos {
namespace {

std::atomic<bool> adapter_destroyed{false};

void VerifyShutdownCompleted() {
    if (!adapter_destroyed.load(std::memory_order_acquire)) {
        std::_Exit(2);
    }
}

class ShutdownTestAdapter final : public BaseAdapter {
public:
    explicit ShutdownTestAdapter(const std::string& interface_name)
        : BaseAdapter(interface_name, "ShutdownTestAdapter", LogLevel::Off) {}

    ~ShutdownTestAdapter() override {
        adapter_destroyed.store(true, std::memory_order_release);
    }

    std::unordered_map<int, Bus*> GetBuses() override {
        return GetKnownBusesSnapshot();
    }

    bool Ok() override {
        return true;
    }

protected:
    void Send(const MotorMessage&) override {}
};

}  // namespace
}  // namespace encos

int main() {
    using encos::EncosDriverManager;
    if (std::atexit(encos::VerifyShutdownCompleted) != 0) {
        return 3;
    }
    auto& manager = EncosDriverManager::Instance();
    const std::string interface_name = "manager-shutdown-throw-test";
    manager.CreateAdapterWithFactory(interface_name, [&interface_name] {
        return new encos::ShutdownTestAdapter(interface_name);
    });
    encos::DriverManagerTestAccess::SetDeletionHook(
        manager, [](EncosDriverManager::DeletionStage stage) {
            if (stage == EncosDriverManager::DeletionStage::BeforeAdapterReceiveDrain) {
                throw std::runtime_error("injected shutdown failure");
            }
        });
    return 0;
}
