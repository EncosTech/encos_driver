# Ethercat 插件

Linux 和 Windows 均使用 `Ethercat`，不再注册 `EthercatWindows`。
原连接串 `EthercatWindows:网卡名:...` 需改为 `Ethercat:网卡名:...`，
构建选项中的 `ethercatWindows` 需改为 `ethercat`。

- `ethercat_adapter.*`：适配器、循环线程和线程优先级。
- `ethercat_handle.*`：SOEM 从站配置、PDO 交换、OP/WKC 检查及断线恢复。
- `transport/linux/`：动态模式通过 fd broker 获取原始套接字；静态模式直接 `ecx_init`。
- `transport/windows/`：将网卡友好名称或 GUID 转为 pcap 设备名，调用 `ecx_init`；也接受完整 pcap 设备名。
- `../utils/ethercatBase/`：与 EthercatIGH 共用的 PDO 编解码、发送队列。

Windows 使用 SOEM 抓包后端，需要提供对应抓包驱动和运行库。
错误恢复与 Linux 一致：失去有效 WKC 后 `Ok()` 返回 false，继续交换并尝试恢复，
确认所有从站 OP 且 WKC 有效后恢复正常；主动停止不会被恢复逻辑撤销。

旧插件目录应移除此前安装的 `EthercatWindowsPlugin.dll`，避免动态目录扫描发现旧二进制。

测试：常规 `ENCOS_BUILD_TESTS=ON` 包含恢复、PDO 与发现测试。
也可用 `ENCOS_ETHERCAT_BUILD_TESTS=ON` 单独构建 `EthercatTests`，
便于交叉编译验证公共恢复及 PDO 测试。

## 本次合并验证

- Linux 动态构建通过；插件测试 160 项通过，2 项 vcan 测试因无虚拟接口跳过。
- Linux 静态构建通过；独立恢复/PDO 测试 36 项全部通过。
- Windows MinGW 交叉编译通过；Wine 下 `EthercatPluginSmoke` 通过，覆盖插件枚举、
  网卡枚举和无效接口构造失败后的正常退出；DLL 加载、入口检查与卸载也通过。
- Windows 独立 GoogleTest 程序在当前 Wine 环境中启动后重复报告运行库临界区等待，
  30 秒超时且没有测试结果输出；不能据此宣称 Windows 恢复测试通过。
- 未进行本次改动的 Windows 原生系统或 EtherCAT 真机通信测试。

日志：`build/ethercat-merge-*.log`、`build-windows/ethercat-merge-*.log`；
静态验证目录：`/tmp/encos-ethercat-static`。
