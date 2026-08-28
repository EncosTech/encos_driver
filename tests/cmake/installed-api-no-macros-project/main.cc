#include <encos/encos_driver.h>

#if defined(MakeAdapter) || defined(UnloadAdapterByInterfaceName) || defined(DeleteAdapter) ||    \
    defined(DeleteBus) || defined(DeleteMotor) || defined(DeleteBattery) || defined(DeleteImu) || \
    defined(DeletePms) || defined(DeleteGlove)
#error "Driver compatibility API entries must not be macros"
#endif

int main() {
    auto* volatile make_adapter = &encos::MakeAdapter;
    if (make_adapter == nullptr) {
        return 1;
    }
    return encos::DeleteAdapter(nullptr) || encos::DeleteBus(nullptr) ||
                   encos::DeleteMotor(nullptr) || encos::DeleteBattery(nullptr) ||
                   encos::DeleteImu(nullptr) || encos::DeletePms(nullptr) ||
                   encos::DeleteGlove(nullptr)
               ? 1
               : 0;
}
