# Tracy 性能分析指南

本文介绍如何采集并分析 EncosMotorDriver 的主线程与 EtherCAT 周期线程性能数据。

## 1. 构建带 Tracy 的库

Tracy 默认关闭。使用独立构建目录并启用 `ENCOS_ENABLE_TRACY`：

```bash
cmake -S . -B build-tracy -DCMAKE_BUILD_TYPE=Release -DENCOS_ENABLE_TRACY=ON
cmake --build build-tracy -j
```

首次配置时，CMake 会通过 `FetchContent` 把固定版本的 Tracy 获取到 `external/tracy`，并编译
`TracyClient.cpp`。本库始终启用 Tracy 的 on-demand 模式：仅在收集器成功连接后产生 zone 和
锁事件，未连接时不会持续累积 trace 数据。应使用 Release 构建和实际的控制负载进行分析；
Debug 构建的优化状态与额外检查会使测量结果失真。

## 2. 构建 Tracy 查看器

客户端与查看器应使用同一份 `external/tracy` 源码，以避免 Tracy 协议版本不兼容。

### 2.1 默认 Wayland 后端

Wayland 开发库版本足够新时，可使用：

```bash
cmake -S external/tracy/profiler -B external/tracy/profiler/build \
  -DCMAKE_BUILD_TYPE=Release
cmake --build external/tracy/profiler/build -j
./external/tracy/profiler/build/tracy-profiler
```

### 2.2 Wayland 兼容性问题

若编译 `BackendWayland.cpp` 时提示 `wl_display_dispatch_timeout` 未声明，说明系统的
Wayland 开发库版本不足。无需修改 EncosMotorDriver 或 Tracy 源码，可改用 X11/GLFW 后端：

```bash
cmake -S external/tracy/profiler -B external/tracy/profiler/build-x11 \
  -DCMAKE_BUILD_TYPE=Release -DLEGACY=ON
cmake --build external/tracy/profiler/build-x11 -j
./external/tracy/profiler/build-x11/tracy-profiler
```

请使用新的 `build-x11` 目录，不必删除失败的 Wayland 构建目录。第三方依赖发出的
`CMP0069` developer warning 不会导致构建失败；`-Wno-dev` 只能隐藏该警告。

另一种方案是升级系统的 Wayland 开发库后继续使用默认后端。

## 3. 实时采集

1. 启动 `tracy-profiler`。
2. 启动链接了 `build-tracy` 中库的实际控制程序。
3. 在 Tracy 欢迎页输入 `127.0.0.1:8086`，点击 **Connect**。客户端程序启动后才可连接；连接
   成功前的执行不会记录到 trace。
4. 确认已连接后，先让系统预热数秒，再执行可复现的控制负载并采集 10 至 30 秒。
5. 在问题复现时暂停时间线，或断开连接后保存 trace。

优先在与被测程序同一台机器上采集。若从远端连接，应确保 TCP 端口可达，并仅在可信网络中
暴露采集端口。

## 4. 命令行采集与归档

需要保存可复现样本、在无图形环境采集或进行自动化时，使用 `tracy-capture`：

```bash
cmake -S external/tracy/capture -B external/tracy/capture/build \
  -DCMAKE_BUILD_TYPE=Release
cmake --build external/tracy/capture/build -j

./external/tracy/capture/build/tracy-capture \
  -a 127.0.0.1 -o normal-run.tracy -s 20
```

`-s 20` 表示采集 20 秒。使用 `tracy-profiler` 的 **Open saved trace** 打开生成的
`.tracy` 文件。建议至少保留一份正常基线和一份问题复现 trace，确保两次负载、周期参数和
运行环境相同。

## 5. 事件与分析顺序

### 5.1 EtherCAT 周期

首先在 Frame time graph 中选择 `EtherCAT` frame。默认周期为 1 ms，因此应优先检查
明显超过 1 ms 的 frame，再在时间线中查看其子区间：

| 事件 | 含义 | 异常时的首要检查方向 |
|---|---|---|
| `EtherCAT::TakePendingSendQueue` | 获取待发业务消息与输出帧 | 提交竞争、队列积压 |
| `EtherCAT::BuildFrame` | 打包业务消息并写入 PDO 输出区 | 单周期消息量、打包成本、PDO 大小 |
| `EtherCAT::WriteProcessData` | SOEM 或 IGH 发送过程数据 | 网卡驱动、发送路径 |
| `EtherCAT::ReadProcessData` | SOEM 或 IGH 接收过程数据 | 链路响应、从站状态、WKC 与超时 |
| `EtherCAT::MessageCallback` | 输入解码、消息路由与用户回调 | 回调阻塞、解码和路由开销 |

当总周期变长而这些子区间都较短时，应转而检查操作系统调度、线程抢占与锁竞争。

### 5.2 主线程提交路径

用户控制线程会看到以下 zone：

| 事件 | 含义 |
|---|---|
| `Adapter::SubmitDeviceMessage` | 单个设备消息进入 Adapter 提交路径 |
| `Adapter::SubmitBusMessages` | 批量消息进入 Adapter 提交路径 |
| `Adapter::Commit` | Adapter 级软同步批次提交 |
| `Bus::Commit` | 单条 Bus 的软同步批次提交 |
| `Adapter::SetSyncMode` / `Bus::SetSyncMode` | 切换同步模式并可能释放积压消息 |

这些 zone 覆盖同步与异步提交。在 Statistics 窗口中同时查看 **With children**（包含下层调用）
和 **Self only**（排除子调用）：前者用于找端到端提交延迟，后者用于识别该层自身开销。

### 5.3 锁竞争

Tracy 会记录本库发送与 EtherCAT 回调路径中的全部锁：

- `Adapter::submit_mutex`：Adapter 提交序列化与同步模式切换。
- `Bus::outgoing_mutex`：Bus 待发缓存与同步批次。
- `EtherCAT::send_mutex`：EtherCAT 待打包消息与待发帧队列。
- `EtherCAT::callback_mutex`：接收回调的读取与更新。

`motor_mutex` 是用户控制调用的上层锁；分析范围从其下层的 Adapter 提交开始。SOEM 和 IGH
主站库内部锁暂不纳入 Tracy 监测。

在时间线中启用 Locks。红色区间表示线程正在等待锁，黄色表示持锁期间已有其他线程等待。
点击锁事件可查看持锁者与被阻塞线程：

- 主线程等待 `send_mutex`：EtherCAT 周期取队列或构帧正在占用锁。
- EtherCAT 线程等待 `submit_mutex` 或 `outgoing_mutex`：用户线程正在进行提交或同步模式切换。
- 长时间黄色区间：应检查持锁范围内是否执行了不必要的拷贝、回调或阻塞操作。

## 6. 推荐判读流程

1. 在 Frame time graph 中选取一个最慢的 `EtherCAT` 周期。
2. 在时间线中比较 `BuildFrame`、写、读和回调四段的耗时。
3. 观察同一时间范围内的锁颜色和等待链。
4. 在 Statistics 中按总耗时、调用次数和平均耗时排序；再用 Find zone 观察耗时分布与长尾。
5. 将异常 trace 与正常基线在 Compare 视图中比较同名 zone。
6. 每次只改变一个变量，例如消息数量、同步模式、回调内容或 EtherCAT 拓扑，再重新采集。

## 7. 采集注意事项

- 不要用 Debug trace 代表部署性能。
- 采集时避免同时运行会大量占用 CPU、磁盘或网络的程序。
- Trace 数据保存在内存中；只采集定位问题所需的时间，避免长时间采集占用大量内存。
- 用户状态回调运行在 Adapter 接收线程。回调中的文件、网络或重计算会直接体现为
  `EtherCAT::MessageCallback` 变长。
- 若需要检查 EtherCAT 通信异常，除 Tracy 时间线外还应同时保存 WKC、从站状态和链路日志。
