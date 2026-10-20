// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include <encos/encos_driver.h>

#if defined(MakeAdapter) || defined(UnloadAdapterByInterfaceName) || defined(DeleteAdapter) ||    \
    defined(DeleteBus) || defined(DeleteMotor) || defined(DeleteBattery) || defined(DeleteImu) || \
    defined(DeletePms) || defined(DeleteGlove)
#error "Driver compatibility API entries must not be macros"
#endif

int main() {
    // MakeAdapter 已重载，取地址需显式选择重载；两个重载都必须可寻址
    using MakeAdapterFunction = encos::BaseAdapter* (*) (const std::string&, const std::string&,
                                                         const std::string&, encos::LogLevel);
    using MakeAdapterExtendFunction =
        encos::BaseAdapter* (*) (const std::string&, const std::string&, const std::string&,
                                 encos::LogLevel, const nlohmann::json&);
    auto* volatile make_adapter = static_cast<MakeAdapterFunction>(&encos::MakeAdapter);
    auto* volatile make_adapter_extend =
        static_cast<MakeAdapterExtendFunction>(&encos::MakeAdapter);
    if (make_adapter == nullptr || make_adapter_extend == nullptr) {
        return 1;
    }
    return encos::DeleteAdapter(nullptr) || encos::DeleteBus(nullptr) ||
                   encos::DeleteMotor(nullptr) || encos::DeleteBattery(nullptr) ||
                   encos::DeleteImu(nullptr) || encos::DeletePms(nullptr) ||
                   encos::DeleteGlove(nullptr)
               ? 1
               : 0;
}
