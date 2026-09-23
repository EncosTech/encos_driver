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

EtherCAT 层根据从站输出 PDO 字节数识别网关类型：

- `86` 字节：classic CAN，2 路 bus，每路 3 个槽位
- `336` 字节：CAN FD，3 路 bus，每路 8 个槽位
- `896` 字节：CAN FD，8 路 bus，每路 8 个槽位

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
