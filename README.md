# example_soem

这是一个基于 C 和 SOEM 的最小 EtherCAT 电机控制示例，用于通过 EtherCAT 网关发送 Encos
电机 CAN 报文。

## 构建

```bash
git submodule update --init external/SOEM
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 运行权限

可以使用 root 运行：

```bash
sudo ./build/example_soem_demo eth0 spd 0 2 0 11 2 1.0 2.0 1
```

也可以给 demo 二进制授予 raw socket 权限：

```bash
sudo setcap cap_net_raw,cap_net_admin+ep ./build/example_soem_demo
./build/example_soem_demo eth0 spd 0 2 0 11 2 1.0 2.0 1
```

## 编号规则

所有对外输入编号都从 0 开始：

- `slaveId`：SOEM 从站顺序减一
- `busId`：网关内部总线编号
- `slot`：该总线内的 PDO 电机槽位编号

日志里会额外打印 SOEM 原始从站号，即 `slaveId + 1`。

## 支持的 PDO 网关格式

EtherCAT 层根据 PDO 字节数识别网关类型。

- `86` 字节：classic CAN，2 路 bus，每路 3 个槽位
- `336` 字节：CAN FD，3 路 bus，每路 8 个槽位
- `420` 字节：CAN FD，3 路 bus，每路 10 个槽位，双向均需为 420 字节
- `896` 字节：CAN FD，8 路 bus，每路 8 个槽位
- `1120` 字节：CAN FD，8 路 bus，每路 10 个槽位

四种 CAN FD 布局共用同一种分片格式。每槽 14 字节，包括 4 字节小端 CAN ID、
1 字节 FRAME、1 字节整帧长度和 8 字节数据。FRAME 的 bit0～3 分别表示 EXT、
FD、BRS、RTR。超过 8 字节时设置 bit4，并在 bit5～7 填入从 0 开始的分片索引。
例如标准 FD+BRS 的 20 字节帧使用三个槽，FRAME 为 `0x16/0x36/0x56`，
三个槽的长度字段均为 20，末槽的后 4 个数据字节填零。

`ec_master_send_can_frame()` 将完整的 `EcCanFrame` 提交给主站队列，支持
1～64 字节。调用时只设置低四位标志。每路按提交顺序发送，每帧的全部分片占用
同一 PDO 中的连续槽。队首整帧放不下时，该路后续报文一起顺延。主站队列最多存放
64 帧，返回成功表示已入队。原有 `ec_master_send_packet()` 仍使用指定电机槽位
提交最多 8 字节的报文。两种发送接口混用时，队列使用固定电机槽之外的连续空槽。

`ec_master_cycle_with_can_handler()` 接收 CAN FD 布局上的完整帧，并将从站编号、
总线编号和包括 FD、BRS 在内的报文标志传给回调。应用需传入实际使用的循环周期
（微秒）。分片 0 开始重组，同键分片可在当前和下一 PDO 集齐，最长等待两倍周期。
收到坏 WKC 或离开 OP 时丢弃尚未完成的重组。原有 `ec_master_cycle()` 使用
1 ms 周期，继续处理短电机、IMU 和电池报文。

## Demo 用法

```text
example_soem_demo <ifname> <op> <slaveId> <busId> <slot> <motorId> <model> [--eff] [--canfd] [--flag <value>] [args...]
```

默认发送 classic CAN 标准帧，即 `eff=0 canfd=0`，对应 `flag=0x00`。如果电机信息里
`eff=1`，在 `model` 后添加 `--eff`；如果 `canfd=1`，添加 `--canfd`。也可以用
`--flag 0x06` 直接指定底层 `MotorConfig.flag`。

示例：

```bash
sudo ./build/example_soem_demo eth0 spd 0 2 0 11 2 1.0 2.0 1
sudo ./build/example_soem_demo eth0 pos 0 2 0 11 2 0.5 2.0 3.0 1
sudo ./build/example_soem_demo eth0 get-param 0 2 0 11 2 5
sudo ./build/example_soem_demo eth0 spd 0 2 0 11 2 --canfd 1.0 2.0 1
```

### IMU 监控

示例也支持只接收并打印 IMU 数据，不发送电机或电池控制报文：

```bash
sudo ./build/example_soem_demo eth0 imu-monitor <slaveId> <busId> [imuIndex]
```

`imuIndex` 默认为 `0`，有效范围为 `0` 到 `9`。程序会在 IMU 状态变化时输出加速度、角速度、欧拉角和四元数。

demo 在发送选定报文后，会以 1 ms 周期运行 200 次收发循环。正式应用应替换为自己的实时循环。

`model` 是 `include/example_soem/motor_layer.h` 中 `MotorModel` 枚举的数值。

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
只有实际观察到全部从站 OP 且 WKC 有效后，`master.operational` 才恢复为 true。
`ec_master_cycle()` 返回 false 表示本周期没有有效业务数据，不表示应关闭主站。
`ec_master_send_packet()` 在未就绪期间返回 false，调用者可在恢复后提交新命令。

SOEM 在原上下文中执行 SAFE_OP+ERROR 确认、SAFE_OP 到 OP 请求、从站重配置及丢失
从站恢复。状态检查通常间隔 50 ms，坏 WKC 后下一周期立即检查。写入 OP 请求本身
不会被当作恢复成功。恢复调用可能延长周期，不提供断联期间的硬实时保证。

示例保持原有 5000 周期运行时长，首次就绪后发送一次命令。已发送的命令不会在
重连后自动重放；应用需要持续控制时，应在就绪后提交新的业务命令。
新增 recovery 测试使用模拟主站接口，不需要硬件；真实网线拔插恢复仍需上机验证。

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

SOEM 在确认 SAFE_OP 后、首次过程数据发送及 OP 请求之前同步写入 SDO。
任一写入失败，初始化立即失败并关闭连接。应用层 PDO 循环启动前已完成配置。
断联恢复沿用现有流程，不额外重放时序 SDO；如果网关断电丢失配置，需重新初始化主站。

无硬件测试 `*_adapter_config_tests` 校验目标映射、写入顺序和长度、采样点换算、
非法及重复配置，以及各步骤失败时停止下发。实际固件参数生效仍需硬件验证。
