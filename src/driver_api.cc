#include "encos/encos_driver.h"
#include "encos_version.h"

extern "C" const char* encos_get_version() {
    return ENCOS_PROJECT_VERSION;
}

namespace encos {

BaseAdapter* MakeAdapter(const std::string& adapter_type, const std::string& interface_name,
                         const std::string& logger_name, LogLevel log_level) {
    return EncosDriverManager::Instance().CreateAdapter(adapter_type, interface_name, logger_name,
                                                        log_level);
}

bool UnloadAdapterByInterfaceName(const std::string& interface_name) {
    return EncosDriverManager::Instance().DestroyAdapterByInterfaceName(interface_name);
}

bool DeleteAdapter(BaseAdapter* adapter) {
    return EncosDriverManager::Instance().DestroyAdapter(adapter);
}

bool DeleteBus(Bus* bus) {
    return EncosDriverManager::Instance().DestroyBus(bus);
}

bool DeleteMotor(Motor* motor) {
    return EncosDriverManager::Instance().DestroyMotor(motor);
}

bool DeleteBattery(Battery* battery) {
    return EncosDriverManager::Instance().DestroyBattery(battery);
}

bool DeleteImu(Imu* imu) {
    return EncosDriverManager::Instance().DestroyImu(imu);
}

bool DeletePms(Pms* pms) {
    return EncosDriverManager::Instance().DestroyPms(pms);
}

bool DeleteGlove(Glove* glove) {
    return EncosDriverManager::Instance().DestroyGlove(glove);
}

}  // namespace encos
