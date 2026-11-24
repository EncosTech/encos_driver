# 版本变更记录

### 1.0.0

#### Feature

- 建立 Motor、Bus、Adapter 三层核心模型，完成电机报文与控制参数转换。
- 建立 EtherCAT 主站、从站处理和插件加载框架。
- 增加插件搜索路径、安装规则、基础库和插件构建目标。

#### Fix

- 修正 PVT 参数写入后本地参数未同步的问题。
- 修正单位换算和 EtherCAT 从站 ID 打包。

#### Test

- 增加 Bus、Motor、Adapter、EtherCAT 和插件加载测试。

### 1.3.2

#### Breaking Changes

- 公开 C++ 接口从小写命名和旧参数体系迁移到新的命名、构造和返回类型体系。
- 安装头文件目录、插件目标和部分 Motor 控制签名变化，依赖旧接口的工程需要重新编译适配。

#### Feature

- 增加 USB 转 CAN、SocketCAN、SLCAN 和 EtherCAT 直连插件。
- 增加接口查询、跨平台插件路径、spdlog 日志、CPack、bench、标定和自动校准。
- 增加多平台 CI、安装后缓存刷新和外部依赖本地化。

#### Fix

- 修正接收解码、扩展 CAN ID、权限、端序、延迟、安装、CPack、CMake 兼容性和 Windows 运行问题。
- 修正 SLCAN 枚举、插件 interface 查询、依赖缺失和发布构建问题。

#### Test

- 增加 Motor 控制、插件、安装和多平台构建测试。

### 1.4.0

#### Feature

- 增加自动校准零点流程及校准结果状态处理。

#### Fix

- 同步校准实现和插件配置，修正校准参数更新。

### 1.4.1

#### Feature

- 调整自动校正的状态推进和重试逻辑，提高校正稳定性。

### 1.4.2

#### Fix

- 修正 Ubuntu 22.04 安装配置和依赖检查。

#### Test

- 增加对应平台安装验证。

### 1.5.0

#### Breaking Changes

- Motor 控制方法的反馈参数和校准/停止调用约定发生变化，旧版按原签名调用的代码需要重新编译适配。

#### Feature

- 增加自动反馈模式和状态缓存更新路径。

#### Test

- 增加自动反馈和状态更新测试。

### 1.5.1

#### Feature

- 增加 Joint、Joint 配置、Joint 状态和 Motor/Joint 状态转换能力。

#### Test

- 增加 Joint 适配和状态测试。

### 1.5.2

#### Fix

- 修正 Joint、Bus、Adapter 和 EtherCAT 状态处理边界。

### 2.1.0

#### Breaking Changes

- 公共 C++ API 全面切换为大写命名接口，旧的小写 Adapter、Bus、Motor、Joint 和日志入口不再提供。
- Direct 插件合并到统一 CAN/EtherCAT 插件，旧插件目标和名称不再适用。
- Adapter、Bus、Motor 的状态、消息、虚函数和生命周期接口重新组织，旧版派生适配器需要迁移。

#### Feature

- 泛化 JointManager，增加纯 JSON 模式、关节类型 Schema、位置偏置、分阶段校准和状态回调。
- 增加电池、温度、错误状态、新电机型号、CAN-FD、扩展帧、8 路 CAN-FD 和 EC 转 CAN-FD 支持。
- 增加消息去重、重要指令插队、发送聚合、Broker 自动提权、运行时权限检查和统一插件处理。

#### Fix

- 修正主循环空队列、旧帧重发、MSG2/MSG8、从站消息混用、发现策略、多电机状态和进程退出问题。
- 修正 CAN-FD 丢包、实时延迟、零点/偏差读取、插件发现、CMake 依赖和 ARM/IGH 构建问题。

#### Refactoring

- 抽取 EtherCAT 基础处理层，清理重复逻辑。
- 将电机参数改为从 CSV 生成，统一插件命名和代码规范。

#### Performance

- 优化热点路径、空包写入和高频发送延迟。

#### Test

- 扩展 Joint、CAN-FD、EtherCAT、状态缓存和 bench 拥塞测试。

### 2.1.1

#### Fix

- 修正 Ubuntu 20.04 CI 环境下的构建和格式检查兼容性。

### 2.1.2

#### Feature

- 增加 stress 工具实时刷新能力。
- 调整持续集成产物上传路径。

### 2.1.3

#### Fix

- 修正实时性相关处理和调度边界。

### 2.1.4

#### Fix

- 为 DEB 包补充目标操作系统版本标识。

### 2.1.5

#### Feature

- 创建同名 Adapter 时复用已有实例，避免重复建立传输资源。

#### Documentation

- 增加并完善使用指南。

### 2.1.6

#### Feature

- 增加电机类型枚举与字符串的双向转换接口。

### 2.2.0

#### Breaking Changes

- 分离的 CAN-FD 扫描入口合并为带参数的统一扫描接口，旧扫描调用需要迁移。

#### Feature

- 增加 CAN 模式配置、设置位置和清零命令。
- 更新 CAN-FD 电机扫描策略。

#### Fix

- 修正 CAN-FD 固件版本识别误判。

### 2.2.1

#### Feature

- 调整 IGH 许可和构建配置。

#### Fix

- 修正 IGH 构建及 CI 问题。

### 2.2.2

#### Feature

- 增加 FakeAdapter，用于无真实硬件时创建和驱动虚拟电机。
- 增加静态库构建和静态插件加载路径。

### 2.2.3

#### Fix

- 修正扫描流程误把电池识别为电机并发送电机命令的问题。

### 2.2.4

#### Breaking Changes

- 日志公开类型改为库自有 Logger/LoggerPtr 和 LogLevel，使用旧 spdlog logger 类型的代码需要迁移。
- Adapter/Bus 的消息读取接口和部分公开索引类型变化，旧签名调用需要重新编译。

#### Feature

- 增加统一平台兼容层，覆盖 Linux、Windows 和 Emscripten。

### 2.2.5

#### Breaking Changes

- Adapter 和 Bus 的 Read 接口增加显式读取缓冲区参数，旧的无缓冲区调用需要补充读取类型。

#### Feature

- 增加电机型号、参数范围和 FakeAdapter 自动创建电机快照相关接口。

#### Test

- 增加 FakeAdapter 自动创建和快照行为测试。

#### Documentation

- 增加 FakeAdapter 自动创建电机设计说明。

### 2.3.0

#### Feature

- 发布 WASM 绑定和 npm 包，覆盖 Adapter、Bus、Motor、Battery、IMU 与 FakeAdapter。
- 增加 stdout、spdlog 和 Emscripten console 日志后端。

#### Fix

- 修正 npm 发布、WASM 测试、构建标签、脚本权限和测试失败问题。

#### Refactoring

- 拆分日志公共逻辑，重构插件构建和加载流程。

#### Build / CI

- 增加原生、无 spdlog、WASM 和 ARM 构建验证。

#### Test

- 增加 WASM 运行时、FakeAdapter、Battery、IMU 和包布局测试。

#### Documentation

- 增加构建文档，明确日志后端和 WASM 使用方式。

### 2.4.0

#### Feature

- 增加 RelayAdapter、WebSocket RelayServer 和虚假接收绘图能力。
- 增加基于 CMake 版本变化和测试结果的发布触发条件。

#### Refactoring

- 拆分 RelayServer 与 fake plot，并将 emd 能力并入 motor CLI。

#### Fix

- 修正 relay 构建和 ARM CI 超时问题。

#### Test

- 调整 RelayWs 测试边界。

### 2.4.1

#### Build / CI

- 改为使用已通过测试的构建产物发布。
- 限制 ARM runner 并行编译数，降低内存溢出风险。
- 重命名构建工作流并统一发布入口。

#### Test

- 清理过时的 bench 配置测试。

#### Documentation

- 删除过时的 bench/nanobench 配置说明。

### 2.4.2

#### Feature

- 增加 FakeAdapter 运行态参数读取、速度直接跳转和公开快照接口。

#### Fix

- 修正 FakeAdapter 快照初始位置、方向相关加速度、零点设置和 CAN 初始化。

#### Documentation

- 归档 FakeAdapter 运行态和 relay/emcli 拆分设计。

### 2.4.3

#### Feature

- RelayWs 解析启动响应中的总线数量并预创建 Bus。

#### Test

- 增加 CAN sample point 重配置边界测试。

### 2.4.4

#### Fix

- 将 RelayWs Bus 创建延后到 GetBuses，避免 shared ownership 尚未建立时触发 bad_weak_ptr。
- 析构时主动释放远端 session，并保留已创建 Bus 的 shared_ptr。

### 2.4.5

#### Feature

- 为 WASM 暴露设置零点接口，并接入运行时和 JavaScript 绑定。

### 2.4.6

#### Fix

- 统一源码和脚本格式，消除格式检查差异。

### 2.4.7

#### Fix

- 修正 WASM 模块导入路径，保证生成产物可被目标运行时加载。

### 2.4.8

#### Feature

- 增加 FakeAdapter 误差开关，支持正常反馈和异常反馈测试。

### 2.4.9

#### Feature

- 增加第二代关节配置参数并同步 Motor/Joint 参数映射。

### 2.4.10

#### Fix

- 修正位置控制模式下速度和力矩积分异常累积。

### 2.4.11

#### Test

- 修正 WASM 测试用例、脚本和测试环境配置。

### 2.5.0

#### Feature

- 增加 IMU 和 Battery 驱动、状态模型、命令接口及 Adapter 访问入口。
- 增加 IMU/Battery 的 WASM API、Node 绑定和类型声明。
- 扩展安装导出、FakeAdapter 和版本同步配置。

#### Test

- 增加 IMU、Battery、WASM 绑定和 Adapter 行为测试。

### 2.5.1

#### Build / CI

- 调整版本发布和构建产物的同步流程。

### 2.5.2

#### Breaking Changes

- npm 包名改为 scoped package `@encos/encos-driver`，旧包名的安装、导入和发布配置需要更新。

#### Build / CI

- 增加 npm 版本一致性检查和 staging 打包流程。

### 2.5.3

#### Test

- 调整平台相关浮点测试容差，避免合法结果被误判。

### 2.5.4

#### Fix

- 修正外部设备响应干扰电机扫描的问题。

#### Build / CI

- 修正对应 CI 场景。

### 2.5.20

#### Feature

- 增加跳过扫描时的告警输出。
- 增加版本头文件和版本一致性检查目标。

#### Fix

- 清理旧的 ecg 构建耦合，修正版本同步边界。

#### Test

- 增加电流量程缓存、待创建对象、Broker 权限和 USB Serial 发送测试。

### 2.5.21

#### Fix

- 修正 WASM 发布产物准备流程。

### 2.5.22

#### Fix

- 调整 ARM 平台浮点计算和验证条件。

### 2.5.23

#### Test

- 放宽 ARM 速度编码浮点舍入容差。

### 2.5.24

#### Feature

- 增加 PMS 设备、状态访问和控制接口。

### 2.5.25

#### Fix

- 修正设备生命周期异常延长和无法结束的问题。

### 2.5.26

#### Fix

- 增加温度反馈中值滤波，抑制偶发比特翻转。

### 2.5.27

#### Fix

- 增加温度反馈限幅，抑制瞬时异常值。

### 2.5.28

#### Build / CI

- 修正持续集成配置和检查脚本。

### 2.5.29

#### Fix

- 修正生命周期变化导致滤波器失效的问题。

### 2.5.30

#### Feature

- 增加滤波器启停配置。

### 2.5.31

#### Feature

- 增加电机数据 CSV/Zstd 日志、日志文件配置和压缩写入路径。

### 2.5.32

#### Feature

- 增加 CPU isolation 配置脚本。

### 2.5.33

#### Feature

- 增加 ExtLinux CPU isolation 恢复逻辑。

### 2.5.34

#### Build / CI

- 收紧安装文档白名单，避免无关文档进入安装产物。

### 2.5.35

#### Feature

- 更新电机型号范围映射和生成参数表。
- 保留 CPU isolation 配置及恢复能力。

### 3.0.0

#### Breaking Changes

- Adapter、Bus、Battery 等核心对象从 shared_ptr 所有权接口切换为 DriverManager 管理的裸指针视图，调用方不能再自行持有或销毁对象。
- GetBus、GetBuses、GetMotor 等返回类型、销毁入口和设备回调契约改变，旧版生命周期管理代码需要重写。

#### Feature

- 增加无锁 SPSC 端口和等待后端，为高频收发提供新的同步基础。

#### Refactoring

- 将驱动对象所有权集中到 DriverManager。
- 将外部设备接收帧直接交给设备回调，统一接收路由和销毁流程。
- 将 WASM adapter 生命周期改为事务式管理。

#### Fix

- 修正 FakeAdapter 并发状态、WASM adapter 删除和 DriverManager 退出清理问题。

#### Test

- 增加管理器拥有对象的生命周期测试。

### 3.0.1

#### Refactoring

- 完善统一传输批处理、优先级、SPSC 等待和对象生命周期组合。
- 精简驱动管理器和传输层实现。

#### Fix

- 修正间接依赖路径和对象生命周期边界。

#### Test

- 补充并发销毁和传输生命周期验证。

### 3.0.2

#### Feature

- 增加 Bus 级 commit 入口，使同一总线的消息可以集中提交。

#### Fix

- 修正电池缩放参数和 ARM 测试问题。

#### Documentation

- 补充 Bus 级 commit 的接口约束和使用说明。

### 3.0.3

#### Fix

- 修正扫描与权限处理边界。
- 修复失效测试和权限场景验证。

### 3.0.4

#### Performance

- 优化高频状态处理中的日志开销。

### 3.0.5

#### Build / CI

- 修正 ARM CI 构建和测试环境。

### 3.0.6

#### Test

- 修正测试夹具和断言，使其匹配当前接口行为。

### 3.0.7

#### Fix

- 修正 EtherCAT 丢包后发送队列持续累积。

### 3.0.8

#### Feature

- 增加软件版本查询接口，并贯通 C++、WASM 和插件访问路径。

### 3.0.9

#### Feature

- 增加 Tracy 插桩点，用于分析实时收发和调度耗时。

#### Fix

- 改进 IGH 主站初始化、状态判断和诊断信息。

#### Documentation

- 同步相关接口和构建规格。

### 3.1.0

#### Feature

- 增加 Glove 编码器设备、EtherCAT 整包槽位从站、5 指分区和状态回调。

#### Fix

- 修正 Glove 编码器报文、FMMU 注册顺序、IGH PREOP 误报和回调触发条件。
- 禁用去重时不再错误维护跨周期 EtherCAT 历史。

#### Refactoring

- 统一 WholePacket/Glove 命名，调整 PDO 注册路径和校准 API。
- 简化 Glove 状态类型和测试系统。

#### Test

- 增加 Glove 状态、校准、EtherCAT 槽位和延迟测试。

#### Documentation

- 增加 Glove 延迟测试报告和接口规格。

### 3.1.1

#### Fix

- 修正电机错误码枚举映射，恢复电压高、低压、编码器错误和温度预警编号。

### 3.2.0

#### Breaking Changes

- Glove 从 Bus 上的单体包装器改为 Adapter 下按从站创建的整手 facade，旧 Bus 级创建入口被移除。
- Glove 的创建、销毁、校准回调、状态收集和内部设备所有权改为整手聚合契约。

#### Feature

- 统一管理 5 条内部帧总线、50 个编码器和 5 个校准设备。
- 增加按手套从站创建和销毁的管理器入口。

#### Refactoring

- 重构设备模板、PDO 槽位、校准协调和整手销毁流程。

#### Documentation

- 补充 Glove 所有权模型、设备模板、PDO 槽位约束和校准方案。

### 3.2.1

#### Fix

- 修正程序终止时电机log不会自动停止的问题。

#### Documentation

- 添加版本更新日志。

### 3.2.2

#### Fix

- 修复了CI中新版本检测的问题。
- 统一新驱动名称为EncosDriver。

#### Feature

- Github发布脚本支持选择sha1。

### 3.2.3

#### Feature

- 添加了灵巧手电机参数

### 3.2.4

#### Feature

- 新增一批电机参数

#### Refactor

- 重构设备管理器创建与销毁逻辑，预留插件化空间

### 3.2.5

#### Feature

- 新增一批电机参数

### 3.2.6

#### Feature

- 优化串口通信延迟
- 优化SocketCan通信

#### Fix

- 修复了soem降级后不会自动恢复的问题
- 修复了进程退出时log概率崩溃的问题

### 3.2.7

#### Fix

- 按《ENCOS 调试技术手册》修正抱闸命令为 2 字节，状态查询按单字节解码。
- 修正 `Brake(true)` 为抱紧（`80 00`），`Brake(false)` 为释放（`80 01`），并同步确认包匹配和 Fake 模拟。旧版本按实际发包行为调用的用户需检查调用参数，避免升级后动作反转。
- 保留 `BrakeStatus` 的 `uint16_t` 返回类型，返回协议原始状态：0 抱紧、1 释放。

### 3.2.8

#### Fix

- 抱闸控制改用调试手册 §9.1.4 的电磁抱闸模式：`Brake(true)` 发送 `75 00 00` 抱紧，`Brake(false)` 发送 `75 00 01` 释放。
- 对应 §10.6 类型 6 回报，仅接受无错误且状态匹配的 `C0 00` / `C0 01`，不再匹配 §9.1.5 的 `B2` 回报。
- 同步 Fake 模拟、使用说明和原始报文回归测试。

### 3.2.9

#### Fix

- 回报帧 1 的位置（16 位）和速度（12 位）改为按对应电机的 `PVTRanges.position`、`PVTRanges.speed` 最小值和最大值解码，移除固定范围。
- 回报帧 1 的电流（12 位）统一按 `PVTRanges.current.min/max` 解码，支持非对称范围；修复本地 PVT 电流范围更新后反馈缩放仍使用独立缓存的问题。
- 同步控制返回值与异步状态使用一致的解码规则，并保护接收线程读取的位置、速度、电流范围快照，避免与范围更新并发冲突。
- Fake 回报帧 1 编码同步使用电机的 PVT 范围。

#### Refactor

- 移除独立的 `current_range` 缓存和解码参数，以 `MotorPVTRanges` 统一提供帧 1 解码范围。
- 保留 `SetCurrentRange()` 并标记为弃用；兼容调用将本地 `PVTRanges.current` 设置为 `{-range, range}`，不发送固件命令。建议改用 `SetDriverPVTRanges()` 指定电流最小值、最大值。

#### Test

- 增加六种控制接口的帧 1 解码、范围更新后的异步状态、非对称电流范围端点及中间值、弃用接口兼容行为的回归测试。

#### Documentation

- 更新使用说明，明确帧 1 的位置、速度、电流解码范围及 `SetCurrentRange()` 迁移方式。

### 3.2.10

#### Fix

- EtherCAT 正常运行阶段累计 WKC 丢包，连续 20 个坏 WKC 后才进入降级恢复流程，避免单个丢包立即退出 OP。
- 每个坏 WKC 都输出日志，并在达到阈值时记录进入降级模式的原因。
- 调整 EtherCAT 错误和恢复日志，使从站状态变化更容易定位。

#### Test

- 增加 WKC 阈值、坏包累计和恢复流程回归测试。

### 3.2.11

#### Fix

- 恢复阶段的坏 WKC 不再计入正常 OP 链路的降级阈值，避免从站恢复过程中再次被错误降级。
- 保持恢复期间的 PDO 交换，待从站状态和有效 WKC 同时确认后恢复 OP 标志。

#### Test

- 增加恢复阶段 WKC 计数和状态确认测试。

### 3.2.12

#### Fix

- EtherCAT 启动和恢复时先确认从站状态，再处理 WKC 异常，避免启动阶段的无效 WKC 被误判为链路丢包。
- 恢复阶段不消费待发送写队列，恢复到有效 OP PDO 交换后再继续发送上层消息。
- 修正 SAFE_OP、PREOP 和从站状态读取的恢复判断，避免缓存状态误导恢复流程。

#### Test

- 增加启动状态检查、SAFE_OP/PREOP 恢复、长时间中断、写队列保留和 WKC 窗口测试。

### 3.2.13

#### Fix

- EtherCAT adapter 创建后等待运行循环确认从站处于 OP 且收到有效 PDO WKC，再允许上层创建电机和读取参数。
- 启动阶段不再使用固定延时作为就绪判断，避免电机参数查询早于 EtherCAT 链路稳定而超时。
- 保持 EtherCAT 断开后的自动恢复、20 个坏 WKC 降级阈值和逐包错误日志行为。

#### Test

- 补充 EtherCAT 启动与恢复回归测试，并在实机上验证 adapter 启动、从站扫描和电机位置参数读取。

### 3.3.0

#### Breaking Changes

- Linux 与 Windows 的 SOEM 插件统一为 `Ethercat`，移除 `EthercatWindows`。原连接串中的 `EthercatWindows` 和构建插件列表中的 `ethercatWindows` 分别改为 `Ethercat`、`ethercat`；Windows 部署目录需移除旧的 `EthercatWindowsPlugin.dll`。

#### Feature

- 新增 `Ethernet` 适配器，通过 IPv4/UDP 连接以太网转 CAN 网关；Linux 使用 AF_XDP，Windows 使用 Winsock UDP，支持动态和静态构建。
- 支持按有线网卡枚举接口、自动配置专用网卡地址、发现从站及初始化每个从站的 8 路 CAN。主机地址为 `192.168.100.254/24`，从站索引为 IP 最后一段减 1。
- Ethernet 支持 EMR1 报文聚合、有界发送队列、心跳续租和 CAN 错误事件日志；支持经典 CAN 和 CAN FD 标志，当前单条记录的数据载荷上限为 8 字节，每个数据报最多 80 条记录。
- Ethernet 创建时通过 EMM1 确认全部 CAN 通道配置，默认仲裁段 1 Mbps、数据段 5 Mbps；配置失败或超时则终止初始化。
- 增加 Linux 网络辅助程序 capability 授权和 Windows UAC 网卡配置流程，安装及 DEB 升级时自动配置 Linux 辅助程序权限。

#### Fix

- 修复 Ethernet 辅助进程随 Ctrl+C 提前退出导致 XDP 残留的问题，并对可恢复的 UDP 网络错误保持管理线程运行。
- 修复 Ethernet 静态构建及 Ubuntu 22.04 的 libxdp 依赖兼容问题。
- 修复 Windows DLL 导出、依赖链接和插件命名问题；Windows 默认日志改用同步输出，避免退出时异步日志线程池回收阻塞。
- 修复 SocketCAN 配置子进程的网络管理权限传递，兼容不同 `ip` 输出中的接口 UP 状态和 CAN FD 标志。
- DEB 启用共享库依赖自动分析，并显式依赖 `libcap2-bin`。

#### Refactoring

- 将 Ethernet 与 RelayWs 共用的 EMR1 编解码移至 `plugins/utils/emr1`，保留原 `relay/relay_frame.h` 转发入口。
- 统一 EtherCAT 的从站管理、PDO 交换和恢复逻辑，仅在传输层区分 Linux 与 Windows 实现。

#### Test

- 增加 Ethernet 协议、发现、CAN 初始化、UDP 收发与来源过滤、静态模式及 Windows 插件进程退出测试。
- 增加统一 EtherCAT 插件的发现、恢复及 Windows 加载烟测，并扩展 SocketCAN 配置解析与虚拟接口测试。

#### Documentation

- 补充 Ethernet 的平台依赖、网卡配置、协议限制、权限、静态部署和验证记录，以及 EtherCAT Windows 插件迁移说明。
- Windows 验证记录覆盖交叉编译及 Wine；不代表 Windows 原生系统或真机通信验证完成。

### 3.3.1

#### Fix

- 调整 WASM/npm 发布 CI 的日志级别。

#### Documentation

- 补齐 3.3.0 版本变更记录，说明 Ethernet 新插件、EtherCAT 跨平台统一、迁移要求及相关修复。
- 将架构说明 `arch.md`、日志指南 `logging.md` 和版本记录 `changes.md` 加入 CMake 安装及 DEB 包，与 `using_guide.md` 一同安装到 `share/doc/libencosdriver`。
- 保留随包安装的 8 张 SVG 图，供使用指南、架构说明和日志指南引用。

### 3.3.2

#### Feature

- 除 Ethercat 插件外，项目自有代码采用 MIT 许可证；代码文件头增加 SPDX 说明，Ethercat 插件继续使用 GPL v3，第三方依赖保留其许可。
- 安装包随附 MIT 许可说明与 Ethercat 插件的 GPL 完整文本，npm 包使用 MIT 许可；第三方许可指向源码中的原文件；生成器保留代码许可证头，`AGENTS.md` 规定后续维护要求。

#### Fix

- 回移植 `8589429e1197fb1a949fd3673264c4f4a2e97401`：兼容旧电机两字节 A0/C0 抱闸确认，仅接受状态匹配且无错误的两字节回包，拒绝截断、错误状态及较长报文。
- GitHub 发布快照递归排除 `AGENTS.md`、`CLAUDE.md` 和 `.claude`、`.codex`、`.agents`、`.cursor`、`.gemini`、`.opencode`，同时删除目标快照已有的对应文件。

#### Test

- 补充 A0/C0 抱闸确认及错误、状态不匹配、长报文的回归测试。
- 发布脚本测试覆盖根目录和子目录的代理配置排除，以及目标快照旧配置清理。

### 4.0.0

#### Breaking Changes

- 基类 `BaseAdapter` 新增公开虚函数 `UpdateBusExtend(const std::string&)`，会移动 vtable 槽位：用旧版驱动头文件编译的**外部**适配器插件需要重新编译。内置插件与本库同批构建，无影响。
- 设备头文件与实现目录收拢到 `include/device/` 与 `src/device/`，include 路径统一为 `device/<dev>/...`；直接按旧路径包含设备头文件的消费者需要更新。
- 内置插件按类别迁入 `plugins/adapter/`，适配器共享工具迁入 `plugins/adapter/utils/`；新增设备插件目录 `plugins/device/`。
- 电机命令集适配新版《电机 CAN 通信协议说明 V1.0.0》，同时保持旧版固件可用：驱动在构造时查询固件版本（0x32），据此选择命令与解码语义。
- 删除 `Motor::GotoLimit`、`Motor::GotoZero`、`Motor::Calibrate` 与 `src/device/motor/motor_limit.cc`。
- 删除新版协议不再支持的命令与对应接口：`Motor::SetAcceleration`（配置 0x01）、`Motor::SetKt`（配置 0x04）、`MotorParameter::Acceleration`(0x05)、`Version`(0x1E) 与 `struct Version`、`FluxKpGain`(0x06)、`FluxKiGain`(0x07)、`FbKpGain`(0x08)、`PdGain`(0x09)。配置通信模式（0x02）与查询抱闸状态（0x25）按要求保留。
- `MotorPVTRanges` 更名为 `MotorProperties`，旧名保留为 `[[deprecated]]` 别名；`Motor::GetPVTRanges`/`Motor::SetDriverPVTRanges` 更名为 `Motor::GetProperties`/`Motor::SetProperties`，旧名保留为 `[[deprecated]]` 转发。`Bus::GetMotor`、`EncosDriverManager::CreateMotor` 与 `FakeMotorSnapshot::ranges` 的签名同步换用新类型。
- `MotorError` 追加 `OutputEncoderError`(9)、`FetOverTemperature`(10)、`OverSpeed`(11)、`TemperatureWarning`(12)；既有成员数值不变。新版固件的 6/7/8/9/31 与旧版固件的 6/7/8 含义不同，由驱动按固件版本映射。
- 新版固件下 `Motor::Brake` 不发送任何报文并直接返回 `false`（协议已移除电磁抱闸控制），`Motor::Stop` 把三种 `MotorStopMode` 统一映射为 CTRL_MODE 2 并以 `current` 作为制动电流上限。
- **所有**电机初始化路径（型号、显式 `MotorProperties`、按索引查询固件）都会额外发送一帧固件版本查询；旧版固件不应答，需等一个短超时（`kFirmwareProbeTimeout`，100 ms）后判为旧版。型号初始化路径不再是零 CAN 流量。
- npm 的 `Bus.getMotor(index, model)` 改为返回 `Promise<MotorWrapper>`，调用方需使用 `await` 等待固件探测完成。同一 Runtime 的创建与异步控制需依次等待，重叠调用会返回明确错误。

#### Feature

- 除 Ethercat 插件外，项目自有代码采用 MIT 许可证，代码文件补充 SPDX 说明；原开源插件和第三方依赖保留其许可，安装包与 npm 包随附许可证。

- 适配器缓存命中不再丢弃扩展配置：同一 `interface_name` 二次 `MakeAdapter` 时，新 `extend` 中 `buses` 的条目经 `BaseAdapter::UpdateBusExtend` 增量应用到既有实例。顶层公共参数无法重新应用，被忽略并记录警告；管理器不解析参数语义，只按非空字符串转发。
- 新增 `CreateAdapterWithFactory(interface_name, extend_json, factory)` 重载；原二参数重载转发空串，既有调用点无需改动。更新在对象锁之外、适配器自身的在飞引用保护下执行，不会与 `DestroyAdapter` 竞态；更新抛异常时不撤销也不销毁既有实例。
- `AdapterExtend` 新增按总线取值的 `BusOverride(slave, bus)`（只返回该总线在 `buses` 中的条目、**不合并**顶层公共参数）与 `Common()`；增量更新路径据此实现“只认 buses”的语义。
- `Ethernet`、`Ethercat`、`EthercatIGH`、`GsUsb` 四个插件实现 `UpdateBusExtend`：部分字段合并到该总线当前生效时序之上，逐条重配，不做跨总线回滚。其余插件继承默认空实现（记录诊断并返回 false）。
- 修复 `Ethercat` 与 `EthercatIGH` 的静态构建扩展配置入口：两者的 `plugin.cmake` 此前未声明 `ENCOS_PLUGIN_STATIC_FACTORY_EXTEND`，且 `CreateWithExtend` 收的是 `AdapterExtend` 而静态注册表传字符串，静态模式下 extend 会被静默忽略。现改为与 Can/Slcan/GsUsb 一致的字符串签名。
- 新增外部设备插件机制：`include/device/device.h` 的抽象 `Device` 基类、`include/device/device_plugin.h` 的 C ABI 描述符与 `MakeDeviceDriver` 入口，运行期以扁平目录下 `lib<Type>Device.so` 后缀与 `MakeDeviceDriver` 符号双重判别加载，并校验 `abi_version`。
- 管理器新增 `CreateDevice`/`DestroyDevice`/`GetAvailableDeviceTypes`，`Bus` 新增 `GetDevice`；外部设备复用既有设备生命周期引擎、operation gate 在飞保护与级联删除。
- CMake 插件发现按 adapter/device 分类，分别生成适配器与设备静态注册表；`plugin.cmake` 新增 `ENCOS_PLUGIN_KIND`/`ENCOS_PLUGIN_DEVICE_TYPE` 与目录一致性校验。
- 新增测试设备插件 `plugins/device/testDevice/`，仅在 `ENCOS_BUILD_TESTS=ON` 时可用（由插件检查函数门控），不进入安装与 DEB。
- CI 拆分测试构建与 DEB 打包构建：打包构建使用 `ENCOS_BUILD_TESTS=OFF` 的独立产物目录，测试专用插件不会进入发布包。
- 设备入口去掉索引：`Bus::GetDevice(type, config)` 与 `EncosDriverManager::CreateDevice(bus, type, config)` 以 `nlohmann::json` 自定义配置取代原 `idx` 参数；设备身份为 `(bus, type, config)`，配置不同即独立实例。`DeviceCreateContext` 的 `index` 字段改为 `config`，`Device::Index()` 改为 `Config()`，`fill_route_ids` 改为接收规范化配置串。新增 `nlohmann::_json` 依赖（DEB 依赖 `nlohmann-json3-dev`，安装包配置导出 `find_dependency`）。
- 新增现代 C++ 设备插件包装层 `include/device/device_plugin_builder.h`：`encos::DevicePlugin` 链式 builder（`Routes`/`Create`/`Destroy`/`ClaimBus`/`Capabilities`），配置直接以 JSON 传递，作者无需接触 C ABI 或手写解析。
- 新增"整总线认领"能力：能力位 `kDeviceCapabilityClaimBus` 与整总线标识符 `kDeviceClaimBusRouteId`；认领设备接收本总线所有未被其它设备路由命中的帧（含系统帧 `0x7FF`），精确路由优先、认领不独占、同一总线至多一个认领设备。
- 新增构建选项 `ENCOS_ENABLE_DEVICE_PLUGINS`（原生默认 ON，Emscripten/wasm 必须 OFF）：关闭时设备插件入口不声明不编译、公共头不依赖 `nlohmann/json`、设备插件目录扫描跳过。
- `add_emd_plugin` 的安装消费模式放宽清单要求：静态注册表字段仅源码树模式需要，安装环境下只需检查函数、类型名与动态目标名。
- 新增 `scripts/verify-installed-device-plugin.sh` 与 `tests/cmake/installed-device-plugin-project` 样例工程：在仅安装 DEB、无驱动源码的环境中构建外部设备插件并完成运行期加载验证（可用 `-DENCOS_ENABLE_PACKAGE_VERIFY_TESTS=ON` 注册为 CTest）。
- 型号表 `src/device/motor/motor_models.csv` 新增 `brake` 布尔列（当前 71 行全部为 `false`，待硬件侧确认）；`MotorProperties` 新增 `brake` 与 `fw_version` 字段，`brake` 只在按型号初始化时由型号表给出；新增 `Motor::HasBrake()`，且 `Motor::SetProperties` 不覆盖 `brake`/`fw_version`（与既有的 `kt` 处理一致）。
- 新增新版协议查询：`MotorParameter::ProductId`(0x30)、`HardwareVersion`(0x31)、`FirmwareVersion`(0x32) 返回 `uint32_t`（版本为 BCD `0xYYMMDDHH`），`MotorParameter::EncoderAngle`(0x27) 返回 `MotorEncoderAngle`（校准标志与原始/校准角度）。这些命令在旧版固件上超时抛 `std::runtime_error`，与其它参数查询一致。
- 新增 `Bus::StartAutoAssignCanId(uint16_t)` 与 `Bus::StopAutoAssignCanId()`：新版协议 2.5 的自动分配 CAN ID 广播（0x08），无即时应答，分配完成后需重新上电并重新 `ScanMotors()`。
- 按固件版本分支的语义：温度反馈解码（旧版 `(raw-50)/2`、新版 `raw-55`）、故障码映射、`Stop`/`Brake` 行为、速度环与位置环增益倍率（配置 0x0D/0x0E 与查询 0x21/0x22，旧版 ×1e-5、新版 ×0.002）。
- `SetCommunicationMode`（配置通信模式 0x02）只存在于旧版协议，旧版帧格式不变，新版固件上不发送任何报文并直接返回 `false`。
- 假适配器 `FakeMotorSnapshot` 新增 `fw_version`（`nullopt` 模拟不认识 0x32 的真机旧固件、`0` 为旧版语义但不等待超时、非 0 为完整新版固件）与产品标识/硬件版本/编码器角度字段；按固件版本编码温度反馈；补齐电流环/速度环/位置环增益查询与写入的真实存取。

#### Fix

- GitHub 发布快照递归排除 `AGENTS.md`、`CLAUDE.md` 和 `.claude`、`.codex`、`.agents`、`.cursor`、`.gemini`、`.opencode`，同时清理目标分支已有的对应文件。

- 修复 WASM 电机创建期间异步固件探测被同步包装器提前读取，导致无效句柄、测试无响应和 Asyncify 状态损坏的问题。
- 合并同一 Bus 包装器的重复创建请求；创建期间拒绝同步销毁适配器，并在创建恢复后检查包装器是否已销毁，失败后可重试。
- WASM CI 增加测试进程与任务超时，避免阻塞后无限等待。

#### Test

- 更新 Fake 与串口测试的初始化固件查询计数，增加固件探测超时、重复创建、销毁竞态和异步调用重叠的回归测试。

### 4.0.1

#### Build / CI

- Windows 构建与消费统一使用 vcpkg 管理依赖，文档提供 MSVC `x64-windows` 和 MinGW-w64 `x64-mingw-dynamic` 配置，以及从私有 registry 安装 `encos-driver` 的方式。
- 更新 SOEM 子模块，将其构建为内部静态库；该修正随源码提供，无需在 vcpkg port 中重复补丁。
- `ENCOS_PLUGIN_INSTALL_LIBDIR` 改为可配置的 CMake 缓存变量，便于包管理器调整插件安装位置。

#### Fix

- 修复 Windows 动态库未正确导出 `encos_get_version` 的问题，使版本查询入口与公共声明一致。

#### Breaking Changes

- Windows 源码安装不再复制 fmt、spdlog、zstd 等依赖 DLL，也不再写入用户级 `PATH` 或 `ENCOS_PLUGIN_PATH`。消费项目需使用 vcpkg applocal 部署依赖，并显式配置插件路径；私有 registry 提供的 `encos_deploy_plugins()` 可将插件及 helper 部署到程序旁。

#### Documentation

- 重写 Windows 构建、安装与消费说明，区分源码安装和 vcpkg 包部署职责。

### 4.1.0

#### Breaking Changes

- 调整软同步的消息范围：电机仅 `PVTControl`、`PosControl`、`SpdControl`、`CurControl`、`TorControl` 五类控制指令等待 commit；`Stop`、`Brake`、参数读写、初始化、改 ID、复零，以及 IMU、PMS、总线扫描和 ID 管理请求均即时提交到插件发送路径。Battery、Glove 和自定义 Device 保持原有策略。
- 普通电机响应和 ACK 的单次等待时限由 500ms 缩短为 **200ms**，`SetPos`、`ResetZeroPos` 原有的 1000ms ACK 等待也统一为 200ms。固件版本探测保留 100ms，参数查询原有重试策略及 `SetCanTimeout` 设置的电机端看门狗不变；带重试的调用总耗时可能超过 200ms。
- EtherCAT 同 ID 消息不再覆盖已占用槽位，每条消息使用独立空槽。每次 commit 可填充此前普通消息帧中对应 bus 的剩余槽位，满帧后顺延；同一 bus 的不同 commit 保持批次边界。既有高负载队列裁剪和设备端口容量策略保留，不承诺过载时每条消息都发送。

#### Feature

- 新增基于 Node-API 8 的 `.node` 原生后端，沿用现有 JavaScript 高层对象和 Promise 接口，提供 Adapter、Bus、Motor、Battery、Imu、Pms、Glove、Fake、设备管理、状态回调、日志及线程优先级等内置功能；自定义 Device 插件接口不在原生 JS 包本次开放范围内。
- 新增三个独立原生平台包：`@encos/encos-driver-native-linux-x64-gnu`、`@encos/encos-driver-native-linux-arm64-gnu`、`@encos/encos-driver-native-win32-x64`。包内携带匹配版本的驱动与插件，无需预装系统 EncosDriver；Windows 包包含 Ethercat，实际使用仍需宿主安装兼容的 Npcap。
- 新增集合包 `@encos/encos-driver-native`，通过精确版本的 `optionalDependencies` 选择平台包，统一导出原生 API 和完整 TypeScript 声明。检查系统、架构、libc 及包版本，不支持的平台或缺失依赖会明确报错，不自动回退 WASM；原 `@encos/encos-driver` 继续作为独立 WASM 包。
- 原生 npm 包增加 `checkPermissions()` 与 `requestPermissions()`：Linux 检查包内 helper 的 capability，按需通过 `pkexec` 调用授权脚本并复查；Windows 返回无需授权的结果。npm 安装阶段不提权，也不修改 Node 可执行文件权限。
- Linux 发布增加 glibc 2.28 基线的 amd64、arm64 产物，同一负载生成 DEB、RPM 和 tar.gz。主包统一携带 SDK 与八种适配器插件，不拆分 IGH、Ethernet 插件包；fmt、spdlog、zstd、libcap 及 Ethernet 用户态依赖静态链接，IGH 用户态库随包提供。

#### Fix

- 修复软同步开启后，电机初始化、查询、配置及管理命令滞留到 commit 才发送而导致的超时。
- 修复 EtherCAT 打包时同 ID 槽位覆盖可能使 Stop 或查询消息被后续消息替换的问题；SOEM 与 IGH 共用此修正。
- Linux 插件入口按精确 C 符号解析，避免修改 RPATH 后依赖 ELF 节布局枚举符号导致的加载失败。
- 修复 MinGW 动态库场景下线程退出时 hazard slot 的释放，避免反复创建线程后耗尽槽位。
- 调整 SocketCAN 构建以兼容较旧 Linux 头文件；权限 broker 独立链接，避免设置 capability 后安全加载模式无法解析相邻驱动共享库。

#### Build / CI

- Linux CI 分为源码检查、基线候选构建、发行版包测试和发布阶段。每个架构构建一次发布负载，Ubuntu 22.04/24.04、AlmaLinux 8/9 安装验证同一份候选；发布复用测试过的原始归档，校验提交来源、SHA256 和包内负载，不重新构建发布包。
- 测试专用设备插件仅进入源码测试构建，不进入系统包和 npm 发布负载。CI 验证安装、卸载、tar.gz 重定位、全部插件加载、Fake 指令及下游 CMake SDK 消费。
- 补齐 Windows 原生 runner 的启动、环境与构建脚本，覆盖动态/静态模式、安装检查和包含 Ethercat 的原生 npm 包构建。
- 原生 npm 平台包复用 Linux 基线驱动及 Windows 原生产物；CI 配置 Node 22/24 的真实归档安装、API 和集合包平台选择测试。发布前校验四个 npm 包的版本、入口、依赖及摘要，先发布三个平台包，最后发布集合包。
- 仅 main 分支 push、项目版本变化且依赖检查全部通过时发布；PR 和手动验证不发布。系统包与原生 npm 包发布均等待相应候选测试通过。
- CI 镜像预装编译器、系统测试工具、Node 和锁文件对应的 JS 依赖缓存，AlmaLinux 任务不再临时运行 dnf 安装工具，JS 任务校验锁文件后离线链接依赖。
- 安装包移除 IGH、Ethernet 依赖的源码归档，保留许可证、依赖版本、源码校验信息及所需重链接材料；第三方源码由独立 GitHub 仓库提供。包元数据不声明 IGH 依赖，权限与网络配置工具按发行版声明为可选依赖。

#### Test

- 增加即时/同步发送分流、软同步下扫描与初始化、200ms 反馈/ACK 以及迟到回复回归；覆盖 Stop、Brake、SetPos 和 ResetZeroPos。
- 增加 EtherCAT 空槽填充、同 ID 消息保留、溢出顺序、跨发送周期及 bus/slave 隔离测试。
- 增加 Node 原生接口、64 位整数、回调、对象销毁、Worker 清理、权限函数、包布局和集合包类型声明测试，保留 WASM 单元与浏览器回归。

#### Documentation

- 新增 Linux 可移植发布说明、原生 npm 包使用指南与 API 映射；更新软同步、超时、EtherCAT 打包和日志诊断文档。
- 明确运行边界：glibc 基线不覆盖 musl/Alpine；IGH 仍需宿主配置匹配的内核主站；Ethernet XDP/BPF 仍依赖内核、网卡及权限。容器插件加载和 Fake 测试不等同于真机通信验证。

### 4.1.1

#### Feature

- 新增全局 C++ 接口 `encos::SetMotorRetryCount(unsigned int)` 与 `encos::GetMotorRetryCount()`，控制所有电机 `GetParameter` 的超时重试次数。默认额外重试 1 次，设为 0 时只发送一次；每次尝试仍最多等待 200ms。配置读写线程安全，每次查询开始时读取一次，不影响已开始的调用，也不为控制、写参数、扫描或日志操作增加重试。

#### Fix

- 调整 SOEM 与 IGH 共用的 EtherCAT 发送队列：即时请求到达时直接打包到待发送帧空槽，不受软同步 commit 代次限制，避免有空槽时仍被推迟到下一帧。commit 填充对应 bus 的剩余槽位，容量不足时顺延；同一 bus 的不同 commit 继续保持批次边界，不覆盖已有消息，也不挤出已排好的控制消息为晚到请求让位。
- 即时请求和已 commit 的控制消息共用待发送帧队列，入队后统一检查积压，超过 3 帧时仅保留最后一帧。裁剪不再等待发送循环，断链期间持续入队也受约束；即时请求可能被裁剪，被裁剪的查询仍按响应超时处理。发送线程每周期直接取出一帧。

#### Build / CI

- CMake 安装导出目标新增包版本属性，使版本升级时主导出文件随之更新，并触发安装器清理旧版本的分配置导出文件。修复安装新版本 RelWithDebInfo 后，Release 消费项目仍通过残留配置链接旧版本动态库、导致新增接口链接失败的问题；同版本的多配置安装仍可共存。

#### Test

- 增加参数查询禁用重试、多次重试、成功后不重试以及配置变更不影响进行中查询的回归测试。
- 更新 EtherCAT 回归测试，覆盖即时请求填充 commit 后空槽、不抢占已占用槽位、跨 commit 代次填槽、即时请求参与统一裁剪，以及不出队和断链期间的积压裁剪。

#### Documentation

- 更新使用指南，说明全局参数查询重试配置、即时请求与软同步控制消息的统一排队及过载丢弃语义。

### 4.1.2

#### Feature

- Node.js 原生运行时新增 `packageRoot` 和 `loadAddon` 选项。打包器及 Node.js SEA 可指定平台包根目录，并自定义 `.node` 加载方式。见 [Node 原生包说明](../npm-native/README.md#打包与单可执行文件sea)。

### 4.2.0

#### Feature

- 新增 `BaseAdapter::GetStatistics()` 和 Node 原生 `adapter.getStatistics()`，提供各总线的 CAN 收发错误计数、采样状态和数据年龄。SocketCAN 和 GsUsb 提供当前计数，Ethernet 保留最近一次错误事件的计数。
- 新增 `RequireCurrentThreadPriority()`，用 `ThreadPriorityError` 和 `MemoryLockError` 区分调度设置与锁页失败。SocketCAN 和 SOEM EtherCAT 适配器构造时等待工作线程初始化，失败时回传异常并回收线程。

#### Test

- 增加总线统计快照、Ethernet 心跳与乱序处理，以及工作线程启动和锁页失败的回归测试。

### 4.2.1

#### Build / CI

- Windows 的 GoogleTest 用例发现超时延长至 60 秒，容纳测试程序首次启动耗时。

#### Test

- 电流范围回归测试改用 `GetProperties()` 和 `SetProperties()`，消除弃用接口警告。

### 4.2.2

#### Build / CI

- 将项目版本更新至 4.2.2，作为 4.2.0 和 4.2.1 累计变更的发布版本。

### 4.2.3

#### Fix

- 电池状态和控制报文统一按 `基础 CAN ID + 2 × 电池索引` 寻址，修复并联场景下第二块电池无法正确收发的问题。

#### Test

- 增加并联电池的状态路由与控制报文寻址回归测试。

### 4.3.0

#### Breaking Changes

- `MotorPackMsg` 和 `PortFrame` 的负载扩至 64 字节。Motor 的地址接口和 Port 的布局也随完整逻辑 CAN ID 支持而调整。应用与插件需要使用本版本头文件重新编译，设备插件 ABI 升至 2。见 [插件指南](plugin.md#64-字节内部消息与传输兼容性)。
- Port 使用 `BasePort::kAnyCanId` 表示通配，显式 `0xFFFFFFFFU` 表示精确地址。用 `IsAnyCanId()` 判断通配状态。
- 子电机集合帧使用 6 槽或 7 槽，每槽由 1 字节 DLC 和 8 字节数据组成。使用旧版无 DLC 集合帧的主机和固件需要同步升级。见 [子电机协议](submotor.md)。

#### Feature

- SocketCAN 和 GsUsb 支持最多 64 字节的 CAN FD 负载，由传输层按标准 DLC 映射补零。Ethernet 和 RelayWs 可与支持的对端协商 EMR2，按完整记录拆包并传输最多 64 字节的 CAN FD 记录。
- 新增 `MakeCanAddress()` 和 `Bus::GetSubMotor()` 等子电机入口。子电机默认启用 CAN FD。Bus 按物理 CAN ID 和同步批次将子命令封装为 CAN FD + BRS 帧，接收反馈时按槽内 DLC 投递给已登记的子电机。见 [子电机协议](submotor.md)。
- 新增 C++ `Bus::GetDexHand()` 和 Node.js `bus.getDexHand()`，支持通过经典 CAN 或 CAN FD + BRS 查询、配置灵巧手网关基址及仲裁域、数据域采样点。保存的采样点在网关重启后生效。见 [使用指南](using_guide.md#47-管理灵巧手网关)。
- 使用 `EC_DEXTEROUS_HAND` 型号创建子电机时，驱动直接加载型号预置参数，省去逐个查询固件版本的步骤。
- `Ethercat` 和 `EthercatIGH` 支持创建时通过顶层 `cycle_period_us` 设置通信周期，默认 1000 µs。见 [插件指南](plugin.md#ethercat--ethercatigh-通信周期)。
- EtherCAT 的 CAN FD PDO 增加固定小槽分片，支持收发最多 64 字节。兼容原有槽宽和总线偏移，并支持三路十槽的 420 字节布局。见 [EtherCAT 通信协议](ethercat_protocol.md)。
- Ethernet 增加协议协商结果和网关运行诊断日志。支持诊断的固件可上报 FIFO 溢出、DMA 超时、UDP 拒绝和 CAN 队列丢弃等计数。Ethernet 数据路径及 SOEM EtherCAT 的 PDO 收发阶段增加 Tracy 采样点。
- Linux 支持设置 `ENCOS_MEMORY_LOCK=none`，在照常设置线程优先级的同时跳过进程锁页。原生驱动日志可通过 `ENCOS_LOG_OUTPUT=stderr` 输出到标准错误。

#### Fix

- Ethernet 管理线程仅查询已登记的总线及电机，修复适配器构造或总线注销期间隐式创建总线的问题。管理会话超时或能力复核失败后，数据工作线程停止，重建适配器后重新协商。
- Linux Ethernet 启动时复用已生效的静态地址，或通过 NetworkManager 为电机专用网卡激活临时的 `192.168.100.254/24`，修复 DHCP 超时清除主机地址后电机反馈中断的问题。支持桌面和交互式 SSH 的 Polkit 授权，SDK 退出后由 NetworkManager 回收临时连接。没有 NetworkManager 管理的专用网卡如需由 SDK 配置地址，可设置 `host_network_exclusive`。见 [Ethernet 主机网卡配置](../plugins/adapter/ethernet/README.md#主机网卡自动配置)。

#### Build / CI

- GitHub Release 改用 Gitea 通用仓库中对应版本的 CI 原件。校验 `amd64`、`arm64` 各自的 DEB、RPM 和 tar.gz 后，按原文件名发布并附带 `SHA256SUMS`。见 [Linux 可移植发布](portable-linux.md)。
- Linux DEB 增加 `libsystemd0` 和 `polkitd` 依赖，RPM 增加 `systemd-libs` 和 `polkit` 依赖。NetworkManager 是可选的运行时服务，DEB 和 RPM 均可在未安装该服务的机器上安装。见 [Linux 可移植发布](portable-linux.md)。

#### Test

- 增加长 CAN FD 帧、EMR2 兼容性、子电机同步聚合与路由、EtherCAT 周期配置、Ethernet 诊断及 GitHub 发布回滚的回归覆盖。
- 增加 Ethernet 网卡配置分支测试，覆盖静态地址复用、DHCP 状态下的临时地址、占用保护和专用网卡自动配置。验证交互式 SSH 授权和异常退出清理，并在根板的 18 个模拟电机上完成强制 EMR1、自动协商 EMR2 各 10 分钟回归，均未出现反馈丢失或心跳超时。

#### Documentation

- 精简 README，补充内网 APT/DNF、GitHub Releases 和源码安装入口。使用指南增加 Linux 实时线程、调度长尾排查和性能调优说明。见 [README](../README.md) 和 [使用指南](using_guide.md)。

### 4.3.1

#### Fix

- Linux Ethernet 按指定网卡和源地址校验网关直连路由，修复 VPN 网段重叠时创建适配器失败的问题。
- NetworkManager 管理的专用网卡在手工断开后仍可激活临时静态连接，并保留禁用自动连接的设备设置。NetworkManager 禁用、休眠或设备状态无法确认时会报告错误。见 [Ethernet 主机网卡配置](../plugins/adapter/ethernet/README.md#主机网卡自动配置)。
