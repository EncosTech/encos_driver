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

### 未发布的更改

#### Documentation

- 增加很多文档
