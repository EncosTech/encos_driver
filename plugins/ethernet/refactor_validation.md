# Ethernet 跨平台重构验证

日期：2026-09-09。范围：`motor_driver` Ethernet 插件及支持其 Windows DLL 加载所需的公共边界。

## 实现结果

- `GetAvailableInterface("Ethernet")` 动态/静态加载器均直接调用 `platform::GetWiredInterfaceNames()`；无须插件存在，也不发送广播。接口能力与在线从站在初始化阶段检查。
- `plugins/utils/emr1/`统一跨插件EMR1编解码与测试；Ethernet的`protocol/`分别保留网关载荷约束、IPv4封装和EMM1管理协议。IPv4 解析无 POSIX 依赖，支持中文、空格及较长的 Windows 网卡名称。
- 共用适配器、初始化、发现、聚合队列与错误日志流程；Linux 采用 AF_XDP，Windows 采用 Winsock UDP。平台辅助程序路径也由后端解析。
- Windows 广播显式指定接口索引及源 IPv4；数据使用已连接 UDP socket、可读事件和发送通知。每个从站独立会话，仍最多80帧聚合。
- 固定 CAN 初始化保持仲裁段 1 Mbps / 75%、数据段 5 Mbps / 87.5%，每次创建适配器重新配置全部8路；不改板端持久配置。
- Linux 保留原线程优先级函数；同一函数增加 Windows 1–99 输入映射，50对应 HIGHEST，不修改进程优先级。
- Windows 网络辅助程序以 UAC 提权，将指定以太网卡设为静态 `192.168.100.254/24` 并持久保存；正确配置时跳过提权。

## 双平台构建中修复的公共问题

1. Windows 网络头文件顺序及友好名称 UTF-8 转换缓冲区需要容纳结尾零字节。
2. IP Helper/UCRT 依赖归入实际使用它们的 Base DLL；spdlog 优先链接标准 CMake 导入目标，传播 external-fmt 等编译定义。
3. 驱动管理器方法按实现所在 DLL 分别导出，避免把主库实现的 `CreateAdapter` 错误导入为 Base DLL 方法。
4. 加载器包含公共 API 声明，确保接口枚举等函数导出；Windows 插件统一无 `lib` 前缀，与加载路径一致。
5. Windows 默认 logger 改为同步多线程安全输出，避免 DLL 退出时回收异步线程池而阻塞。Linux 异步 logger 不变。

## 验证记录

| 验证 | 结果与范围 |
| --- | --- |
| Linux 编译 | 核心库、Ethernet AF_XDP 插件、BPF、辅助程序及测试构建通过 |
| Windows 编译 | MinGW-w64 x86-64 交叉编译核心 DLL、EthernetPlugin.dll、网络配置辅助程序；默认 spdlog 后端启用，通过 |
| Linux 核心测试 | 422/422通过；使用 `setpriv --no-new-privs`，避免主机上已授权的优先级 helper 破坏“无 capability 必须拒绝”的测试前提 |
| Linux 插件测试 | 161项：159通过，2项vcan测试因环境缺少vcan跳过；其中Ethernet相关14项通过 |
| Windows/Wine 测试 | 17项通过：共用协议/初始化/发现，以及真实本机Winsock收发、来源过滤、超长报文拒绝、事件唤醒、临时不可达容错、线程优先级映射 |
| Windows 进程烟测 | CTest 独立进程正常退出：公共API、DLL枚举/加载、无效接口构造失败清理、默认logger回收；15秒超时未触发 |
| 接口枚举 | Linux设置不存在的插件目录后，Ethernet/Ethercat均返回与平台函数一致的4个接口；Windows同类烟测通过 |
| 静态检查 | `format-check`、`tidy`、`git diff --check`通过 |
| 独立审查 | 发现并修复临时UDP错误停止管理线程的回归，复核无剩余Critical/Important；另复核Windows默认logger修复 |

日志：

- Linux `build/ethernet-refactor-build.log`、`ethernet-refactor-core-tests.log`、`ethernet-refactor-plugin-tests.log`。
- Windows `build-windows/ethernet-refactor-build.log`、`ethernet-refactor-ctest.log`，详细CTest输出位于 `build-windows/Testing/Temporary/LastTest.log`。
- 接口枚举 `build/ethernet-refactor-interface-smoke.log`。

Windows CTest 最终运行：`EthernetTests`、`EthernetWindowsPluginSmoke`，2/2目标通过。交叉运行设置了 `CMAKE_CROSSCOMPILING_EMULATOR=/usr/bin/wine`；Windows本机不需要此选项。

## 临时网络错误回归

新UDP封装最初将 Linux `ECONNREFUSED` 等抛出，导致管理线程退出。真实本机测试关闭接收端口，收到ICMP拒绝，原实现稳定失败：`UDP receive failed: 111`。

修复后，明确可恢复的网络错误在两平台返回“暂时发送失败/暂无数据”，保留管理续租；无效描述符等致命错误仍抛出。Linux测试使用同一客户端socket，在服务端重新绑定原端口后继续成功收发。

红绿日志：`build/ethernet-recovery-red.log`、`build/ethernet-recovery-green.log`。Windows也验证了对端端口关闭后socket不会因该错误失效。

## 实测边界与部署

- 本次没有更换板端固件或切换板卡工作模式，没有重跑CAN硬件bench/stress；当前网络物理连接为EtherCAT。
- Windows实机网卡、UAC提权、静态IP持久保存、防火墙以及多从站硬件性能尚未实测。Wine套接字及进程测试不能替代这些验证；MSVC命令已提供，但本次实际编译器是MinGW。
- Linux AF_XDP仍要求单RX队列，接口枚举可能包含虚拟网卡，不保证枚举出的每个接口都支持AF_XDP。
- 本次重新链接了Linux辅助程序，生成文件的capability已清除；下一次硬件运行前执行：

```sh
sudo setcap cap_net_admin,cap_net_raw,cap_bpf+ep build/plugins/EthernetXdpBrokerExecutable
```

修改均留在当前工作区，未提交、安装或发布。

## 目录归属调整回归

公共 EMR1 声明、实现与8项协议测试已移至 `plugins/utils/emr1`；Ethernet与RelayWs均直接引用该模块。
Ethernet的`transport`根目录仅保留平台无关接口，具体文件分别归入`linux/`、`windows/`。
原`protocol/emr1.*`拆为`gateway_payload.*`和`ipv4.*`，区分网关策略与网络封装。

这次目录调整后的Linux/Windows构建均通过。Windows独立测试包含25项测试（原17项加8项共用EMR1测试），
加上独立DLL进程退出烟测，CTest两个目标均通过。Linux插件测试覆盖EMR1、RelayWs和Ethernet。

旧公开头与新规范头已在临时安装目录中同时编译、链接并完成编解码，验证兼容头不会依赖源代码目录。
对应日志为 `build/shared-layout-*.log`、`build-windows/shared-layout-ctest.log`。
