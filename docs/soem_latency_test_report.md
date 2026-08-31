# SOEM EtherCAT 参数读回延迟测试报告

## 摘要

以 SOEM 插件的当前实现和 `emcli bench` 的测试方法为基准，单次参数读取的稳态端到端延迟为约 **3 ms**，即 `3 × 1 ms` PDO 周期。这里的“端到端”从 `Motor::GetParameter()` 发起请求开始，到匹配的 CAN 电机应答经 EtherCAT TxPDO 回到客户端、唤醒同步等待者为止；它不是纯 EtherCAT 在线传输时延。

该结论同时得到实测支持：7 个场景的结果均在 `2,999.91–2,999.97 us/op`。即使 63 个其余电机持续产生 PVT 流量，读回时间仍保持约 3 ms。

## 测试范围与配置

- 测试日期与开始时间：2026-08-06 15:37（UTC+08:00）。
- 执行命令：`emcli bench Ethercat:enxfc1928644276`。
- 适配器：SOEM EtherCAT 插件（`Ethercat`），接口 `enxfc1928644276`。
- 被测电机：扫描结果中的第一个电机；输出为 `CanFD=true`、`CanEFF=false`。
- 拥塞电机：扫描出的其余 63 个电机。
- PDO 循环周期：SOEM 插件默认 `1,000 us`，见 `plugins/ethercat/ethercat_handle.h` 的 `kDefaultLoopPeriod`。

## 测试软件版本

| 组件 | 测试时版本 | 源码提交 |
| --- | --- | --- |
| `emcli`（`motor_cli`） | v1.9.7 | `432d7af` |
| EncosDriver / SOEM EtherCAT 插件 | v3.0.9 | `e60145e` |
| 内置 SOEM | v2.0.0 | `785d7dd` |

## 3 ms 的时序推导

SOEM 循环每 `T = 1 ms` 运行一次。每轮依次取待发送帧、写 RxPDO、调用 `ecx_send_processdata()`、调用 `ecx_receive_processdata()`，然后将 TxPDO 解码结果交给接收回调。相关逻辑位于 `plugins/ethercat/ethercat_handle.cc` 的 `EthercatHandle::Loop()`。

`GetParameter()` 构造 CAN 参数读取请求并调用 `SendAndWait()`；后者先注册响应等待者、再把消息放入 EtherCAT 发送队列，并阻塞到 `Motor::OnMessage()` 收到匹配的应答。实现分别见 `src/motor/motor_parameter.cc` 和 `src/motor/motor.cc`。因此计时覆盖了主站排队、PDO、从站 CAN 转发、CAN 应答、TxPDO 和客户端唤醒。

在本次连续同步读取的稳态相位下，路径跨越三个 1 ms 的采样阶段：

| 阶段 | 发生的工作 | 累计时间 |
| --- | --- | ---: |
| 1. 主站下一个 PDO 机会 | 上一笔读取在接收回调中返回后，下一笔请求才进入发送队列；该轮的组帧已经过去，需等下一个 `PrepareNextFrame()` 将请求写入 RxPDO 并发送。 | `1T = 1 ms` |
| 2. 从站输出与 CAN 转发 | 从站由 `APPL_OutputMapping()` 取出 RxPDO，`APPL_Application()` 调用 `can_process_tx()` 发往 CAN；电机应答进入从站 CAN RX 队列。 | `1T = 1 ms` |
| 3. 从站输入与主站接收 | 随后的输入映射调用 `can_dequeue_input_frames()`，将应答写入 TxPDO；下一次过程数据交换由 SOEM 接收、解码并调用 `Motor::OnMessage()` 完成等待者。 | `1T = 1 ms` |

因此，稳态模型为：

```text
L_readback ≈ T_queue + T_can_forward_and_reply + T_input_pdo
           ≈ 1 ms + 1 ms + 1 ms
           ≈ 3 ms
```

EtherCAT 帧在线传输、CAN/CAN-FD 帧序列化和线程调度所占的是各阶段内的亚周期量。请求若落在不同相位，或从站/CAN 总线出现额外排队，实际值可以偏离该模型；`3 ms` 是这里连续 `GetParameter()` 调用所锁定的稳态路径，而不是所有请求的严格下界。

从站侧的对应代码在 `../Ethercat_8CAN`：SSC 的 `PDO_OutputMapping()` 调用 `APPL_OutputMapping()`，其后应用函数调用 `can_process_tx()`；`APPL_InputMapping()` 则先调用 `can_dequeue_input_frames()` 再复制 TxPDO。CAN 接收回调把帧压入该队列。这个“RxPDO→CAN→队列→TxPDO”的分阶段结构解释了为何一次同步读取不等同于一次 EtherCAT 往返。

SOEM 初始化中确实调用了 `ecx_configdc()`，但该函数的职责是发现支持 DC 的从站并测量传播延迟；当前插件没有调用 `ecx_dcsync0()` 配置 SYNC0。故本报告不将 3 ms 归因于已启用的从站 DC 同步，而归因于现有 1 kHz PDO 和 CAN 网关的管线阶段。

## `motor_cli` 的测试方式

`../motor_cli/src/cli/bench.cc` 中的 `RunBenchCommand()` 按以下方式执行：

1. 创建一个 `Ethercat:enxfc1928644276` 适配器，等待 1 秒后扫描电机。
2. 把第一个扫描到的电机作为被测对象，剩余全部电机作为拥塞对象。因此本次输出的 `Congestion motors: 63` 表示确有 63 个真实扫描电机参与后台负载。
3. nanobench 以微秒为单位计时，每个 epoch 至少执行 100 次。每个样本调用一次同步 `GetParameter()`：
   - `duplicate_delay` 始终读取 Position；
   - `idle_delay` 交替读取 Position 和 Speed；
   - 五个 `congestion_*` 场景也读取同一被测电机，同时后台线程对每个拥塞电机循环调用 `PVTControl<0>(0, 1, 0, 0, 0)`，线程轮次之间分别休眠 10、5、3、2、1 ms。
4. 参数读取抛出异常即记作一次丢包；默认策略在前 50 次中超过 5 次失败、或之后失败率超过 10% 时中止基准。

因此表中的 `us/op` 是一次**同步请求—应答**的耗时，`op/s` 是其倒数；它不测 PVT 下发，也不直接测循环线程的 wakeup 抖动。

## 实测结果

原始输出中的 SOEM 结果如下：

| 场景 | 延迟（us/op） | 吞吐量（op/s） | err% | 总时长（s） |
| --- | ---: | ---: | ---: | ---: |
| Duplicate | 2,999.94 | 333.34 | 0.0 | 3.58 |
| Idle | 2,999.94 | 333.34 | 0.0 | 3.58 |
| Congestion 10 ms | 2,999.96 | 333.34 | 0.0 | 3.58 |
| Congestion 5 ms | 2,999.92 | 333.34 | 0.0 | 3.58 |
| Congestion 3 ms | 2,999.91 | 333.34 | 0.0 | 3.58 |
| Congestion 2 ms | 2,999.95 | 333.34 | 0.0 | 3.58|
| Congestion 1 ms | 2,999.97 | 333.34 | 0.0 | 3.58 |

丢包统计：

| 场景 | 丢包 |
| --- | ---: |
| Duplicate | 0 / 1193（0%） |
| Idle | 0 / 1193（0%） |
| Congestion 10 ms | 0 / 1193（0%） |
| Congestion 5 ms | 0 / 1193（0%） |
| Congestion 3 ms | 0 / 1193（0%） |
| Congestion 2 ms | 0 / 1193（0%） |
| Congestion 1 ms | 0 / 1193（0%） |

## 结论

在当前 SOEM 插件、8CAN 从站固件和 `emcli bench` 的测试条件下，`GetParameter()` 的同步请求—应答往返稳定在约 **3 ms**。该数值与 1 kHz PDO 周期下“主站排队并下发、从站 CAN 转发并接收应答、TxPDO 回传并唤醒客户端”三个阶段的 `3 × 1 ms` 管线模型一致。

实测的 Duplicate、Idle 及五档拥塞场景均为 `2,999.91–2,999.97 us/op`，所有场景的丢包统计均为 `0 / 1193`。因此，在本次 63 个电机的后台 PVT 负载下，没有观察到拥塞导致的额外稳态读回延迟或读回失败。
