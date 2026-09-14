# Ethernet：Linux AF_XDP / Windows Winsock UDP

Linux/Windows 静态或动态适配器，通过普通以太网 IPv4/UDP 承载与 RelayWs 相同的 EMR1 CAN 记录。
Linux 使用 AF_XDP generic/SKB + copy；Windows 使用 Winsock UDP，不依赖 Npcap。
两端共用发现、CAN 初始化、聚合排队、心跳和 CAN 错误日志处理。Windows 的延迟指标需要实机测量，不能沿用 Linux AF_XDP 结果。

## 源文件与依赖边界

| 层 | 文件 | 职责 |
| --- | --- | --- |
| 跨插件协议 | `../utils/emr1/relay_frame.*` | Ethernet与RelayWs共用的EMR1声明、编解码与协议测试 |
| 网关载荷约束 | `protocol/gateway_payload.*` | 调用共用EMR1编解码，检查80帧上限、CAN0–7、方向和flags |
| 网络报文 | `protocol/ipv4.*` | IPv4地址解析、原始IPv4/UDP帧封装与校验和 |
| 共用管理协议 | `protocol/gateway_wire.h`、`protocol/management_protocol.*` | EMM1 字段读写、CAN 错误事件校验，无系统 socket 依赖 |
| 共用业务 | `ethernet_adapter.*`、`ethernet_management.*`、`ethernet_tx_queue.h` | 从站路由、80 帧聚合、有界队列、发现重试、8 路 CAN 初始化、订阅与 logger |
| 通信契约 | `transport/transport.h`、`transport/datagram.h` | 数据帧通道、已连接 UDP 通道、携带入接口/源地址的广播通道 |
| Linux | `transport/linux/` | AF_XDP/BPF/broker、POSIX 管理 UDP 与 IP_PKTINFO 广播 |
| Windows | `transport/windows/` | Winsock、IP Helper 网卡选择、广播接口指定、事件唤醒、UAC 网络配置辅助进程 |

共用EMR1在`plugins/utils/emr1`维护，仍编入Base库并沿用原导出函数，避免改变DLL依赖；旧公开头`relay/relay_frame.h`转发到新位置，安装时同时提供新头。

CMake 的 `EthernetProtocol` 不编译平台通信代码，`EthernetManagement` 按平台选择 UDP 实现，`EthernetAdapterSupport` 按平台选择 AF_XDP 或 Winsock 数据通道。
新增 EMM1 配置时应先在共用协议/管理层实现，不复制到两套通信后端。

`GetAvailableInterface("Ethernet")` 在动态和静态加载器中都直接复用
`platform::GetWiredInterfaceNames()`，不再加载插件或广播探测。该函数返回已启用的有线接口，
可能包含虚拟网卡；AF_XDP 能力检查在创建适配器时进行。在线从站发现仍由适配器初始化执行。

## Linux 构建和使用

依赖 `libxdp-dev`、`libbpf-dev`、`libelf-dev`、`pkg-config` 和支持 BPF target 的 Clang。

```sh
cmake -S . -B build -DENCOS_BUILD_TESTS=ON
cmake --build build --target EthernetPlugin EncosMotorDriverPluginsTests -j8
```

Linux 普通用户首次初始化时，若 helper 缺少 `cap_net_raw,cap_net_admin,cap_bpf+ep`，通过 Polkit 授权将 capabilities 写入实际使用的 helper 文件，然后继续以普通用户运行。授权取消或失败会返回明确错误。需要 `pkexec`、认证代理和 `libcap2-bin`。

`sudo cmake --install build` 及 DEB 安装/升级会自动设置这些权限。权限随文件保留；重新链接或替换 helper 后会重新检查，必要时再次授权。无需用 sudo 运行整个应用。
插件、`EthernetXdpBrokerExecutable`、`ethernet_xdp.bpf.o` 应放在同一插件目录中。

收发线程复用 `utils::SetCurrentThreadPriority(50)`，请求 SCHED_FIFO 50 并锁定进程内存。
动态构建需要为现有优先级 helper 和调用方分别授权，例如工作区 emcli：

```sh
sudo setcap cap_sys_nice=ep build/encosPlugins/ThreadPriorityHelper
sudo setcap cap_ipc_lock=ep ~/motor_cli/build/emcli
```

调用方已有其他 capability 时应合并保留。上述文件重新链接后需重新授权。
设置失败会记录警告并继续通信；只有优先级和锁页均成功才记录成功日志。

网段固定为 `192.168.100.0/24`，掩码 `255.255.255.0`，slave ID 为地址最后一段减1（0–252，不能与主机或其他slave重复）。主机固定使用192.168.100.254；.255是广播地址，两者均禁止分配给从站。

连接只填网卡名，插件通过 UDP 5001 广播发现在线slave，无需在连接串指定IP：

```cpp
encos::SetPluginPath("/home/bismarck/motor_driver/build/plugins");
auto interfaces = encos::GetAvailableInterface("Ethernet"); // 只返回网卡名
auto* adapter = encos::MakeAdapter("Ethernet", "enx6c1ff7bd2e87");
auto buses = adapter->GetBuses(); // 所有已发现slave的8路CAN
auto* bus = adapter->GetBus(9, 4); // 192.168.100.10，slave 9，CAN4
// 使用结束后 encos::DeleteAdapter(adapter);
```

新版核心库和插件必须一起使用。多个slave共享同一AF_XDP队列，各自独立排队和续租；使用UUID检测重复slave ID并报错。运行中新增板卡需重建适配器刷新发现结果。旧`网卡@IP`入口只保留给历史工具兼容。

动态模式的辅助进程负责建立 XSK/UMEM、挂接 BPF，并通过私有 Unix socket 传递文件描述符。
主进程直接操作 AF_XDP 环；辅助进程不转发 CAN 数据。初始化时的普通 UDP 空帧仅用于建立邻居项。
连接退出会清理自己的 XDP 程序；不会覆盖网卡上已经存在的 XDP 程序。

## 静态模式

```sh
cmake -S . -B build-static -DENCOS_STATIC_MODE=ON -DENCOS_ENABLE_INSTALL=OFF \
  '-DENCOS_PLUGINS_LIST=ethernet;fake' -DENCOS_BUILD_TESTS=ON
cmake --build build-static -j
```

静态模式把适配器编入主库，不生成 Ethernet 共享插件或网络配置 helper，
也不读取插件目录。Linux 由当前进程直接配置网卡、创建 XSK/UMEM 并加载 BPF；
BPF 对象在构建时嵌入静态库，无需部署 `ethernet_xdp.bpf.o`。
这些操作与动态 broker 复用 `XdpSession`，权限检查复用 fdBroker 的 capability 工具。
缺少有效权限时直接报错，不弹出 Polkit 授权。

Linux 最终可执行文件需要 `cap_net_raw,cap_net_admin,cap_bpf`；若同时使用实时调度和锁页：

```sh
sudo setcap cap_net_raw,cap_net_admin,cap_bpf,cap_sys_nice,cap_ipc_lock+ep ./your_app
```

以上命令会替换文件上的 capability 集合；应合并应用已有权限，重新链接后需重新授权。
正常销毁适配器会释放 AF_XDP 并卸载本会话挂载的 BPF。静态进程被强制终止时无法执行析构，
调用方应安排正常退出与适配器销毁；遗留 XDP 程序不会被新会话自动覆盖。
Windows 静态模式直接配置网卡，需要以管理员权限运行最终程序；动态模式仍使用 UAC helper。

## 协议与限制

- 每个 UDP 数据报最多 80 条记录；单条 CAN 数据最多 8 字节，支持经典 CAN 和 CANFD 格式。
- flags bit0=EXT、bit1=FD、bit2=BRS、bit3=RTR，复用已有 Relay 编解码器。FD+BRS 为 `0x06`。
- 当前网关仲裁段 1 Mbps、数据段 5 Mbps；BRS 决定该帧是否使用数据段速率，flags 不编码任意波特率值。
- 仅支持 IPv4 直连、无 VLAN、无 IP options、无 IP 分片；绑定网卡队列 0。要求单个RX队列，多队列配置会报错，可使用ethtool将接收队列数设为1。
- 网关为单客户端，插件每秒发送空 EMR1 心跳；换客户端应留出网关约 5 秒租约过期时间。
- 不提供 UDP 重传或可靠送达保证。主机发送队列有界，队列满时报错。
- 每个slave暴露 CAN0–7，主机索引为 `(slave_id << 16) | can_id`。

## 历史硬件验证状态（其中显式IP链接为旧版写法）

2026-09-07，在 HPM6E00EVK 原 EtherCAT 百兆口和板载 CAN4 上，以 USB 转 CAN 模拟应答，
100/100 次 AF_XDP → UDP → CANFD → CANFD → UDP → AF_XDP 往返通过，核对 flags=6 和回包内容。
该结果是模拟节点的真实链路测试，不是电机 bench/stress 性能结果。
换回用户的 CANFD 测试板后仍存在 CAN 错误，bench/stress 尚未完成。


2026-09-08，HPM6E6Y 集成板使用 ENET100/PW MII 内置 PHY0（MDIO 地址 2）及 CAN4，
真实测试板 ID 13、14、15 为经典 CAN 1 Mbps。AF_XDP bench 7 项各 1193 样本，总计 8351，
0 丢包，254–450 us/op。HPM6E6Y 没有 TSW，不可使用 EVK 的 TSW100 配置。

emcli stress 原有接口枚举检查不接受手工 Ethernet 地址，已在
`~/motor_cli/src/cli/adapter_cli_utils.cc` 增加该类型的显式地址支持，地址由插件校验。
当前测试使用工作区编译版（全局安装版尚未替换）：

```sh
~/motor_cli/build/emcli stress --no-inplace-refresh --plugin-path "$PWD/build/plugins" \
  'Ethernet:enx6c1ff7bd2e87@192.168.100.10'
```

本轮 stress 有效测量 55.960 秒，共 167760 帧，最终 0 丢包，2997.83 fps。
未连接总线的自动扫描会产生 CAN 错误/队列忙计数；结果仅覆盖已连接 CAN4 上三个经典 CAN 节点。


## Ctrl+C 与 XDP 清理

Ethernet 插件用独立进程组启动辅助程序，Ctrl+C 由 emcli 处理。
辅助程序在 emcli 停止发送、等待在途回复、释放适配器并关闭控制 socket 后，
执行 Session 析构，按自己持有的 program fd 卸载 XDP，再退出。
修复前辅助程序继承 emcli 进程组，终端 Ctrl+C / timeout SIGINT 会直接终止辅助程序，
跳过析构，留下 XDP；仅向 emcli 的 PID 发 SIGINT 则可正常清理。
历史上只重建插件时不影响 broker capability；本次目录重构会重新链接 broker，需重新授予 capability。

硬件回归脚本：`~/Ethercat_8CAN/tools/test_ethernet_stress_shutdown.py`。
它对自己创建的进程组发送 SIGINT，检查正常退出、辅助程序回收以及网卡 XDP 无残留；
不会覆盖已有 XDP。使用已连接的测试板及已授权 broker：

```sh
python3 ~/Ethercat_8CAN/tools/test_ethernet_stress_shutdown.py \
  --drain-ms 2000 --log /tmp/ethernet-stress-shutdown.log
```

stress 原有 `--drain-delay-ms` 默认为200ms；工作区 emcli 现会显示等待期间额外收到的回复数量。
指定2000可延长收尾，参数不会改变发送阶段速率或补发丢失报文。

## CAN 初始化

每次创建 Ethernet 适配器时，插件会通过 EMM1 / UDP 5001 对每个已发现从站的 CAN0–7 发送固定时序配置：仲裁段 **1 Mbps / 75%**，CANFD 数据段 **5 Mbps / 87.5%**。这组默认参数保存在上位机代码中，板端仍无启动默认值，也不写 Flash。

插件先取得设备 UUID 和配置能力，再逐路等待 CAN_STATE 确认配置成功且返回值一致；每200ms重试，每一步最多等待2秒。只有全部从站、全部通道确认后才启动数据传输。旧固件不支持该协议、配置被拒绝或超时，都会使适配器初始化失败并记录错误；不会在未配置CAN的情况下继续运行。重新创建适配器会重新发送全部初始化指令；相同参数由板端幂等处理。

### 主机网卡自动配置

创建 Ethernet 适配器时，先调用 `EthernetXdpBrokerExecutable --configure-interface <网卡>`，
将该网卡的主 IPv4 设置为 `192.168.100.254/24`、广播地址 `192.168.100.255`，启用接口，
最多等待10秒链路就绪，再发现从站、初始化8路CAN和创建AF_XDP。
`.254` 为主机专用；从站ID范围为0–252。仅指定网卡被配置，不添加默认路由或DNS。
配置保留到被系统网络服务更改或重启；下次创建适配器会再次应用。
请将该网卡作为专用电机网络接口，避免DHCP/NetworkManager同时覆盖地址。

辅助进程需要现有的 capability；重新编译会清除可执行文件上的 capability：

```sh
sudo setcap cap_net_admin,cap_net_raw,cap_bpf+ep build/plugins/EthernetXdpBrokerExecutable
```

配置失败时终止初始化并报告错误。自动配置只在指定网卡创建适配器时执行，
全局枚举不会修改所有网卡。独立Python管理工具可使用 `--bind 192.168.100.254`。

## Windows 构建和部署

使用带 Windows SDK 的 C++17 工具链和 CMake，依赖 fmt、spdlog、zstd；无需 libxdp、libbpf、Clang BPF 或 Npcap。
例如在已配置 Visual Studio 与 vcpkg 的 PowerShell 中：

```powershell
vcpkg install fmt:x64-windows spdlog:x64-windows zstd:x64-windows
cmake -S . -B build-win -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DENCOS_PLUGINS_LIST=ethernet -DENCOS_ENABLE_INSTALL=OFF -DENCOS_ENABLE_RELAY=OFF
cmake --build build-win --config Release --target EncosMotorDriver
```

项目默认启用 Python 生成检查，需要 Python 3。Windows 当前通过构建目录部署，主工程尚未启用 Windows install/CPack。
将核心 DLL 及依赖 DLL 放在上位机程序目录，把 `EthernetPlugin.dll` 和
`EthernetNetworkHelperExecutable.exe` 放到同一插件目录，并调用 `encos::SetPluginPath()`。
多配置生成器的产物通常位于 `Release/` 和 `plugins/Release/`；MinGW 的核心 DLL 名称带 `lib` 前缀。

连接参数为平台枚举返回的 UTF-8 网卡名称，例如 `MakeAdapter("Ethernet", "以太网 2")`。
首次初始化按接口索引启动网络辅助程序，Windows UAC 请求管理员权限，设置所选网卡为
`192.168.100.254/24` 静态地址并启用；配置持久保存，主机不添加默认网关或 DNS。
网卡已处于正确静态配置时跳过提权。取消 UAC、辅助进程失败或地址未生效时，初始化报错。
普通 UDP 数据收发不需要管理员权限；防火墙需允许上位机进程接收设备 UDP 回复。

Winsock 使用绑定本地 IPv4、指定接口索引的已连接 UDP socket，接收端只接受对应设备 IP/端口。
发现报文显式携带出接口信息；每个从站保留独立会话。可读事件和发送通知唤醒工作线程。
Windows 默认 logger 同步输出，避免 DLL 退出时异步线程池回收阻塞；CAN 错误事件仍由独立管理线程记录。其他调用方的日志会在各自调用线程同步输出。Linux 默认 logger 继续异步输出。

两平台都调用 `utils::SetCurrentThreadPriority(50)`：Linux 保持 SCHED_FIFO 与锁页逻辑，
Windows 映射到 `THREAD_PRIORITY_HIGHEST`，不修改进程优先级，也不承诺实时调度。

## 独立插件测试

无需启用全工程测试，也可单独构建 Ethernet 测试（依赖 GTest）：

```sh
cmake -S . -B build -DENCOS_ETHERNET_BUILD_TESTS=ON
cmake --build build --target EthernetTests
ctest --test-dir build --output-on-failure -R '^Ethernet'
```

Windows 还会创建 `EthernetWindowsPluginSmoke`，需同时构建该目标；多配置生成器在构建时加
`--config Release`，CTest 加 `-C Release`。这项独立进程测试检查公共 API、DLL 枚举和构造失败回收，
要求程序在15秒内正常退出，从而覆盖默认 logger 退出阻塞。测试用不存在的网卡名，不会触发网卡配置或 UAC。

## 本次重构验证

见 [跨平台重构验证记录](refactor_validation.md)。历史硬件测量仅证明当时版本的表现，不能视为本次重构或 Windows 的性能测试。

SlaveId = IP 最后一段 − 1：`.1` → `0`，`.10` → `9`，`.253` → `252`。
可分配 IP 和 Flash 中的 IP 配置不变；连接串及 GetBus 使用新的从零编号。
