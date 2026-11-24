# example_igh

这是一个基于 C 和 IgH EtherCAT Master 的最小 EtherCAT 电机控制示例，用于通过
EtherCAT 网关发送 Encos 电机 CAN 报文。

## 依赖

本示例使用 IgH EtherCAT Master 1.6 的用户态接口。目标系统需要安装并启动主站，并提供：

- `ecrt.h`
- `libethercat`
- `/dev/EtherCAT0` 或对应 master index 的设备节点

可以用以下命令快速检查本机开发文件：

```bash
pkg-config --cflags --libs libethercat
```

### 8 路 EC 转 CAN 的 IgH 配置要求

使用 8 路（8bus）版本的 EC 转 CAN 时，需要将 **IgH EtherCAT Master 源码**中
`master/globals.h` 的 `EC_MAX_SII_SIZE` 修改为 `16384`，才能正常工作：

```c
#define EC_MAX_SII_SIZE 16384
```

修改后须重新编译、安装 IgH 主站及内核模块，并停止后重新启动 IgH 服务，
确保加载的是修改后的模块。仅重新编译本示例不会改变该限制。

## 构建

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 运行

`example_igh_demo` 的第一个参数是 IgH master index，不是网卡名。例如 `0` 对应
`/dev/EtherCAT0`。

```bash
sudo ./build/example_igh_demo 0 spd 0 2 0 11 2 1.0 2.0 1
```

如果不使用 root，请确保当前用户有 `/dev/EtherCAT0` 的读写权限。

## DC 同步与周期线程优先级

默认使用 SM 同步、1000 µs 周期，并沿用启动线程的调度策略。全局选项放在主站编号和操作名之间，电机命令和 IMU 监控均可使用。例如以 1 ms 周期、零偏移启动 DC 监控：

```bash
sudo ./build/example_igh_demo 0 --sync dc imu-monitor 0 0
```

| 全局选项 | 行为 |
| --- | --- |
| `--sync sm\|dc` | DC 为所有已识别的网关配置单 SYNC0（AssignActivate 为 `0x0300`），选第一个网关作参考时钟。各网关固件均须支持该模式。 |
| `--period-us N` | 仅用于 DC。主站发送间隔、demo 唤醒周期和 SYNC0 共用此值。传入正整数，换算为纳秒须能放入 32 位无符号整数。在 `long` 为 32 位的平台上，demo 最多接受 1147483 µs。实际可用范围取决于网关固件。 |
| `--sync0-shift-ns N` | 仅用于 DC，默认 0。有符号 32 位纳秒数，绝对值小于一个周期。正值推迟 SYNC0 相位。 |
| `--rt-priority N` | 仅用于 DC，默认给周期线程设置 `SCHED_FIFO 80`。1～99 指定 FIFO 优先级，0 沿用当前调度策略。 |

启动时若请求 FIFO 失败，demo 会关闭主站并在首次业务发送前退出，打印系统错误和 `RLIMIT_RTPRIO`。可用 `ulimit -r` 核对启动环境的实时优先级上限；systemd 服务使用 `LimitRTPRIO`，也可由部署环境授予 `CAP_SYS_NICE`。普通调度联调可显式使用 `--sync dc --rt-priority 0`。

集成到 C 应用时，`ec_master_open_with_configs()` 的第 5 个参数传同步配置，传 `NULL` 使用默认 SM 模式。调用方负责设置实际执行 `ec_master_cycle()` 的线程优先级并按周期唤醒。以下选项选择 2 ms 周期、SYNC0 相位提前 50 µs：

```c
EcMaster master;
const EcMasterOptions options = {
    .sync_mode = EC_SYNC_DC,
    .cycle_period_us = 2000,
    .sync0_shift_ns = -50000,
};
if (!ec_master_open_with_configs(&master, "0", NULL, 0, &options)) {
    return 1;
}
```

每个 DC 周期以单调时钟更新 application time，并在发包前同步参考时钟和从站时钟。时钟调用失败会丢弃待发命令并交换空 PDO；继续调用 cycle，直至同步调用恢复、从站 OP 且 WKC 完整，再提交新命令。

在上述 1 ms 监控示例运行期间，可在另一终端核对实际调度和从站寄存器。将 `<PID>` 替换为 demo 进程号，将 `-p 0` 替换为待查网关的位置：

```bash
chrt -p <PID>                              # SCHED_FIFO，优先级 80
ethercat slaves                            # 各已配置网关应处于 OP
ethercat domains                           # 实际 WKC 等于期望值，且大于零
ethercat reg_read -p 0 -t uint16 0x0980    # 0x0300
ethercat reg_read -p 0 -t uint32 0x09a0    # 1000000 ns
```

## 编号规则

所有对外输入编号都从 0 开始：

- `masterIndex`：IgH master index，例如 `0` 表示 `/dev/EtherCAT0`
- `slaveId`：IgH slave position
- `busId`：网关内部总线编号
- `slot`：该总线内的 PDO 电机槽位编号

## 支持的 PDO 网关格式

EtherCAT 层根据从站 PDO 字节数识别网关类型。

- `86` 字节：classic CAN，2 路 bus，每路 3 个槽位
- `336` 字节：CAN FD，3 路 bus，每路 8 个槽位
- `420` 字节：CAN FD，3 路 bus，每路 10 个槽位，双向必须均为 420 字节
- `896` 字节：CAN FD，8 路 bus，每路 8 个槽位
- `1120` 字节：CAN FD，8 路 bus，每路 10 个槽位

420 字节布局按从站 SII 报告的输入和输出同步管理器注册。每个方向的三路总线
分别从字节偏移 0、140、280 开始。

### 发送与接收完整 CAN FD 帧

所有 CAN FD 布局使用 14 字节槽，每槽仍有 8 字节数据区。最多 64 字节的帧按
顺序占用相邻槽，槽头保存小端 CAN ID、帧标志和整帧字节数。长帧的 `FRAME`
位 4 为分片标志，位 5～7 为分片索引。电机命令的指定槽位和 8 字节载荷格式保持原样。

`ec_master_send_can_frame()` 提交完整帧。周期发送时先保留电机命令指定的槽位，再
按总线的提交顺序放入原始 CAN 帧。队首一帧所需的连续空间不足时，该帧及同路的
后续帧等到后续 PDO。`ec_master_cycle_with_can_handler()` 接收、重组并交付完整帧，
调用时传入实际周期，单位是微秒。`ec_master_cycle()` 使用初始化周期，默认为 1000 µs。DC 模式要求回调接口的周期与初始化值一致。

```c
static void on_can_frame(uint16_t slave_id, uint16_t bus_id,
                         const EcCanFrame* frame, void* user_data) {
    (void) user_data;
    printf("slave=%u bus=%u id=0x%X len=%u\n", slave_id, bus_id, frame->id, frame->len);
}

/* 主站就绪后提交一帧 20 字节的标准 FD+BRS 报文。 */
const EcCanFrame frame = {.id = 0x123, .frame_flags = 0x06, .len = 20, .data = {1, 2, 3}};
bool queued = ec_master_send_can_frame(&master, 0, 0, &frame);
bool ready = ec_master_cycle_with_can_handler(&master, 1000, on_can_frame, NULL);
```

重组按从站、总线、CAN ID、基本帧标志与整帧长度识别。同一组仅保留至下一份有效
PDO，且自首片起满两倍周期即过期。遇到坏 WKC、从站离开 OP 或重连时，未完成的
接收组会被清除。此线格式没有帧序号，无法识别所有同键的新旧分片混合。


## Demo 用法

```text
example_igh_demo <masterIndex> [全局选项] <op> <slaveId> <busId> <slot> <motorId> <model> [--eff] [--canfd] [--flag <value>] [args...]
```

默认发送 classic CAN 标准帧，即 `eff=0 canfd=0`，对应 `flag=0x00`。如果电机信息里
`eff=1`，在 `model` 后添加 `--eff`；如果 `canfd=1`，添加 `--canfd`。也可以用
`--flag 0x06` 直接指定底层 `MotorConfig.flag`。

示例：

```bash
sudo ./build/example_igh_demo 0 spd 0 2 0 11 2 1.0 2.0 1
sudo ./build/example_igh_demo 0 pos 0 2 0 11 2 0.5 2.0 3.0 1
sudo ./build/example_igh_demo 0 get-param 0 2 0 11 2 5
sudo ./build/example_igh_demo 0 spd 0 2 0 11 2 --canfd 1.0 2.0 1
```

### IMU 监控

示例也支持只接收并打印 IMU 数据，不发送电机或电池控制报文：

```bash
sudo ./build/example_igh_demo 0 imu-monitor <slaveId> <busId> [imuIndex]
```

`imuIndex` 默认为 `0`，有效范围为 `0` 到 `9`。程序会在 IMU 状态变化时输出加速度、角速度、欧拉角和四元数。

demo 运行 5000 个收发周期，默认周期为 1 ms。正式应用应替换为自己的
实时循环。

`model` 是 `include/example_igh/motor_layer.h` 中 `MotorModel` 枚举的数值。

## 检查

代码变更后运行：

```bash
bash scripts/run_clang_format.sh --check
bash scripts/run_clang_tidy.sh build
```

## 断联自动恢复

`ec_master_cycle()` 在断联后仍须持续调用。沿用 main 的恢复策略：正常运行的每
100 周期统计窗口累计 20 次坏 WKC 后降级；检测到从站离开 OP 时立即降级。
恢复期间发送空 PDO，丢弃新业务报文、不解析无效输入，并清除缓存外设状态。
只有实际观察到全部已配置从站 OP 且 WKC 有效后，`master.operational` 才恢复为 true。
`ec_master_cycle()` 返回 false 表示本周期没有有效业务数据，不表示应关闭主站。
`ec_master_send_packet()` 在未就绪期间返回 false，调用者可在恢复后提交新命令。

IgH 保持主站激活并持续 receive/process/queue/send，由内核主站状态机自动重新
配置从站。就绪判定同时检查链路、从站数量、全部已配置从站 OP 和完整且非零的域 WKC。

示例保持原有 5000 周期运行时长，首次就绪后发送一次命令。已发送的命令不会在
重连后自动重放；应用需要持续控制时，应在就绪后提交新的业务命令。
新增 recovery 测试使用模拟主站接口，不需要硬件；真实网线拔插恢复仍需上机验证。

IgH 的 OP 检查逐个读取已配置从站的状态，未支持的从站不参与 OP 判定，以兼容混接拓扑。

## 通过 SDO 配置 CAN 时序（v4 网关）

`examples/main.c` 的 `main()` 中使用 `adapterConfigs` 数组逐通道配置。例如：

```c
const EcAdapterConfig adapterConfigs[] = {
    {.slave_id = 0, .bus_id = 0, .bitrate = 1000000, .dbitrate = 5000000,
     .sample_point = 0.75, .dsample_point = 0.875},
    {.slave_id = 0, .bus_id = 1, .bitrate = 500000, .dbitrate = 2000000,
     .sample_point = 0.80, .dsample_point = 0.875},
};
```

从站和 CAN 通道编号均从 0 开始；波特率单位为 bit/s，采样点为 `(0, 1)`
内的小数，写入时四舍五入为千分比整数。每项须填写全部四个时序参数，
同一从站的同一通道不能重复。未列出的通道不会收到时序 SDO。

该功能默认关闭：`main()` 中 `enableAdapterConfig = false`，电机控制和 IMU 监控
均不下发 CAN 时序 SDO，保留网关原配置。`adapterConfigs` 仅作为配置示例，
使用前须按自己的 EC 转 CAN 型号、接线及设备参数修改。

确认型号及固件支持 CAN 时序 SDO 后，将 `enableAdapterConfig` 改为 `true` 并重新编译。
并非所有 EC 转 CAN 都支持此功能；v3 或其他不支持该对象字典的固件应保持关闭。
SDO 失败时程序会提示检查 EC 转 CAN 型号及固件版本；版本过低或不支持时，
请关闭该开关并重新编译后使用。关闭配置不会恢复先前已写入的时序参数。

`ec_master_open_with_configs()` 在初始化期间调用 `ec_master_configure_adapters()`：
先校验整组配置及目标通道，再依次写入 `0x2300 + bus_id` 的子索引 1–4
（仲裁段波特率、数据段波特率、仲裁段采样点、数据段采样点，均为 32 位），
最后写子索引 5（8 位值 1，Apply）。不支持初始化完成后的运行时配置。
失败不会回滚已写入的配置；应用应关闭主站并检查错误，不继续业务发送。

IgH 在 `ecrt_master_activate()` 之前使用 `ecrt_slave_config_sdo32/8()` 注册 SDO，
由主站在进入 SAFE_OP 的过程中下载，且在重新配置从站时按 IgH 流程重放。
注册失败会中止初始化；注册成功不等于从站已接受参数。实际下载被拒绝时应检查
IgH 主站日志。周期循环驱动状态收敛，只有实际观察到 OP 且 WKC 有效后才发送业务报文。

无硬件测试 `*_adapter_config_tests` 校验目标映射、写入顺序和长度、采样点换算、
非法及重复配置，以及各步骤失败时停止下发。实际固件参数生效仍需硬件验证。
