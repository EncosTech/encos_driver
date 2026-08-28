#pragma once

#include <string>
#include <thread>
#include <unordered_map>

#include "adapter/base_adapter.h"
#include "ethercat_igh_handle.h"
#include "export.h"

namespace encos {

/**
 * @brief IGH 主站的直连 adapter（不依赖外部 executable 进程）。
 *
 * 职责与 EthercatWindowsAdapter 一致：
 * 1) 接管 handle 生命周期；
 * 2) 在 BaseAdapter 与 handle 之间转发收发消息；
 * 3) 基于 handle 的 bus 拓扑创建 Bus 对象。
 */
class EthercatIGHAdapter : public BaseAdapter {
public:
    /// 工厂函数，供插件入口 MakeAdapter 调用。
    static EthercatIGHAdapter* Create(const std::string& interface_name,
                                      const std::string& logger_name = "EthercatIGHAdapter",
                                      LogLevel log_level = LogLevel::Info);

    EthercatIGHAdapter(const EthercatIGHAdapter&) = delete;
    EthercatIGHAdapter& operator=(const EthercatIGHAdapter&) = delete;
    ~EthercatIGHAdapter() override;

    std::unordered_map<int, Bus*> GetBuses() override;
    bool Ok() override;

protected:
    EthercatIGHAdapter(const std::string& interface_name,
                       const std::string& logger_name = "EthercatIGHAdapter",
                       LogLevel log_level = LogLevel::Info);

    void Send(const MotorMessage& message) override;
    void Send(const MotorMessages& messages) override;
    void SendSynchronized(const MotorMessages& messages) override;

private:
    /// 独立线程运行 handle 的周期 Loop，避免阻塞上层调用线程。
    void Loop();

    std::shared_ptr<EthercatIGHHandle> ec_master_;
    std::thread loop_thread_;
};

}  // namespace encos

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ENCOS_STATIC_MODE
ENCOS_PLUGIN_API encos::BaseAdapter* MakeAdapter(
    const char* interface_name, const char* logger_name = "EthercatIGHAdapter",
    int log_level = static_cast<int>(encos::LogLevel::Info));
#endif

#ifdef __cplusplus
}
#endif
