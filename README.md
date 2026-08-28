# example_igh

这是一个基于 C 和 IgH EtherCAT Master 的最小 EtherCAT 电机控制示例，用于通过
EtherCAT 网关发送 Encos 电机 CAN 报文。

## 依赖

目标系统需要先安装并启动 IgH EtherCAT Master，并提供：

- `ecrt.h`
- `libethercat`
- `/dev/EtherCAT0` 或对应 master index 的设备节点

可以用以下命令快速检查本机开发文件：

```bash
pkg-config --cflags --libs libethercat
```

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

## 编号规则

所有对外输入编号都从 0 开始：

- `masterIndex`：IgH master index，例如 `0` 表示 `/dev/EtherCAT0`
- `slaveId`：IgH slave position
- `busId`：网关内部总线编号
- `slot`：该总线内的 PDO 电机槽位编号

## 支持的 PDO 网关格式

EtherCAT 层根据从站输出 PDO 字节数识别网关类型：

- `86` 字节：classic CAN，2 路 bus，每路 3 个槽位
- `336` 字节：CAN FD，3 路 bus，每路 8 个槽位
- `896` 字节：CAN FD，8 路 bus，每路 8 个槽位

本示例有意不实现 `340` 字节 8 路 classic CAN 格式。

## Demo 用法

```text
example_igh_demo <masterIndex> <op> <slaveId> <busId> <slot> <motorId> <model> [--eff] [--canfd] [--flag <value>] [args...]
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

demo 在发送选定报文后，会以 1 ms 周期运行 5000 次收发循环。正式应用应替换为自己的
实时循环。

`model` 是 `include/example_igh/motor_layer.h` 中 `MotorModel` 枚举的数值。

## 检查

代码变更后运行：

```bash
bash scripts/run_clang_format.sh --check
bash scripts/run_clang_tidy.sh build
```
