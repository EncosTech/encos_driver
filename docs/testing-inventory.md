# 当前测试项清单

本文档记录重构后的自动化测试与验证入口，并保留已移除的 CMake 元测试清单作为迁移记录。它刻意区分：

- 常规 C++ GoogleTest；
- 保留的独立 CTest 与 WASM/TypeScript 浏览器测试；
- CI 验证、真机 benchmark 与第三方依赖自带测试。

除非另有说明，路径均相对仓库根目录。测试数会随平台、启用的插件、静态/动态模式及工具链可用性变化；不能把某一次配置的 CTest 数量当作固定接口。

## 总览

在本次复核使用的动态 Linux 配置中，`ENCOS_BUILD_TESTS=ON`、Relay 启用、TSan 关闭且检测到 `em++`。 `ctest --test-dir /tmp/motor-driver-test-refactor-dynamic -N` 共列出 **476** 项：

| 类别 | CTest 项数 | 注册方式 | 备注 |
| --- | ---: | --- | --- |
| 核心 GoogleTest | 358 | `gtest_discover_tests(EncosMotorDriverBaseTests)` | 核心库、Motor、设备状态和 WASM Runtime |
| 插件 GoogleTest | 116 | `gtest_discover_tests(EncosMotorDriverPluginsTests)` | 已启用的 Fake、CAN、fd broker、USB Serial、EtherCAT Base、Relay 和动态加载测试 |
| 独立 CTest | 2 | `add_test()` | 关机异常与条件 Emscripten Port 编译 |

`ENCOS_ENABLE_TSAN=ON` 时会额外注册一个带筛选器的 TSan CTest；Emscripten WASM 构建不允许开启 `ENCOS_BUILD_TESTS`，而使用独立 WASM 测试链路。

## 1. 常规 C++ GoogleTest

### 1.1 目标与运行方式

开启 `ENCOS_BUILD_TESTS=ON` 后，`cmake/EncosTests.cmake` 构建两套测试可执行文件：

| 目标 | 内容 | 变化条件 |
| --- | --- | --- |
| `EncosMotorDriverBaseTests` | 核心库、Motor、路由/并发、设备状态与 WASM Runtime | 测试源码位于 `tests/core/`，不包含任何具体 adapter 插件实现测试 |
| `EncosMotorDriverPluginsTests` | 主库加载机制及已启用插件的测试 | 插件在自身 `CMakeLists.txt` 通过 `encos_register_plugin_tests()` 声明源、链接库和私有 include；主工程统一发现 |

两目标都通过 `gtest_discover_tests()` 展开为“每个 GoogleTest 用例一个 CTest 项”，并统一设置 `ENCOS_DISABLE_PRIORITY_GUI=1`。因此日常执行入口仍是：

```bash
cmake -S . -B build-test -DENCOS_BUILD_TESTS=ON
cmake --build build-test -j
ctest --test-dir build-test --output-on-failure
```

测试构建依赖 GTest/GMock、Python 和 `Fake` 插件。`ENCOS_BUILD_TESTS=ON` 同时要求 `ENCOS_ENABLE_PYTHON=ON`。

### 1.2 核心目标覆盖项

下表按源码文件给出测试主题和当前 `TEST`/`TEST_F` 定义数量。这些是维护和拆分测试时的定位索引，不等同于所有构建配置下的最终 CTest 数。

| 领域 | 源文件（用例数） | 覆盖重点 |
| --- | --- | --- |
| 适配器基础与软同步 | `tests/core/test_adapter.cc`（49） | 适配器/总线软同步、原始回调生命周期、状态缓存和滤波、外接设备 |
| 消息路由与扫描 | `tests/core/adapter_routing_test.cc`（6）；`tests/core/bus_port_scan_test.cc`（9） | 多总线隔离、回调串行化、扫描、取消与 CAN FD 优先级 |
| DriverManager 与所有权 | `tests/core/driver_manager_test.cc`（42）；`tests/core/plugin/plugin_ownership_test.cc`（2） | 创建去重、路由、回调重入、删除/级联删除、地址复用与并发 |
| 电池、IMU、PMS | `tests/core/device/*_callback_test.cc`（15） | 直接路由、状态解码、超时、回调重入与资源释放 |
| 端口与并发基础设施 | `tests/core/port_test.cc`、`operation_gate_test.cc`、`platform_sync_test.cc`、`thread_priority_test.cc`（38） | FIFO/覆盖、无锁进度与 ABA、操作登记、互斥和提权 helper |
| 日志 | `tests/core/log_writer_test.cc`；`tests/core/motor/logging_test.cc`（16） | CSV 写入与并发、日志帧和传输 |
| 电机控制、参数和状态 | `tests/core/motor/*.cc`（154） | PVT、位置/速度/电流/扭矩、停止、刹车、范围、CAN ID、参数、日志、状态和应答等待 |
| RuntimeStore | `tests/core/runtime/wasm_runtime_test.cc`（7） | RuntimeStore 句柄、延迟删除和适配器生命周期 |

### 1.3 插件目标覆盖项

| 模式/条件 | 源文件（用例数） | 覆盖重点 |
| --- | --- | --- |
| 主库加载机制 | `tests/core/plugin/plugin_test.cc`（动态）或 `plugin_static_test.cc`（静态） | 动态发现/缓存、插件加载，或静态注册与 adapter 类型查询 |
| Fake | `plugins/fake/test/*.cc`（41） | 模拟运动、自动回包、命令记录、参数策略、帧编码和动态控制接口 |
| Linux CAN | `plugins/can/test/*.cc`（13） | vcan 接收与 CAN socket 时序/重配置命令 |
| Linux fd broker | `plugins/utils/fdBroker/test/fd_broker_privilege_test.cc`（6） | 重启参数、身份鉴权和握手超时 |
| USB Serial | `plugins/usbSerial/test/usb_serial_sender_test.cc`（2） | 重试队列与停止时取消 |
| EtherCAT Base | `plugins/utils/ethercatBase/test/ethercat_base_handle_test.cc`（28） | PDO 映射和上限、CAN/CAN FD 打包与解包、帧队列和同步边界 |
| Relay | `plugins/relayWs/test/*.cc`（23） | 连接与队列、EMR1 帧、URL/响应解析 |

`tests/core/manager_shutdown_throw_test.cc` 不使用 GoogleTest：它是单独可执行文件，确保管理器析构时能包含清理阶段异常，由 CTest 项 `EncosManagerShutdownContainsCleanupExceptions` 调用。

### 1.4 已注册 GoogleTest 完整清单（构建快照）

下表由当前 `build/*Tests[1]_tests.cmake` 的 `gtest_discover_tests()` 结果整理。每一项均为可直接传给 `ctest -R '^名称$'` 的 CTest 名称；同一测试套件的具体用例合并在一个单元格中。核心/插件归属来自生成该项的测试目标，领域分类可回看 1.2 和 1.3 的源码索引。

<!-- GTEST-REGISTERED-ITEMS-START -->
| 分类 | 测试套件 | 具体 CTest 项 | 测什么（本套件全部用例） |
| --- | --- | --- | --- |
| 插件 GoogleTest | `CanSocketSetupTests` | `CanSocketSetupTests.DownStateRequiresReconfiguration`<br>`CanSocketSetupTests.MissingFdRequiresReconfiguration`<br>`CanSocketSetupTests.ParsesCanFdTimingFields`<br>`CanSocketSetupTests.ParsesClassicCanTimingFields`<br>`CanSocketSetupTests.SetupCommandIncludesTargetTiming`<br>`CanSocketSetupTests.TargetConfigMatchesItself`<br>`CanSocketSetupTests.UpAndDownCommandsAreFormatted`<br>`CanSocketSetupTests.WrongBitrateRequiresReconfiguration`<br>`CanSocketSetupTests.WrongDbitrateRequiresReconfiguration`<br>`CanSocketSetupTests.WrongDsamplePointRequiresReconfiguration`<br>`CanSocketSetupTests.WrongSamplePointRequiresReconfiguration` | 解析 CAN/CAN FD 当前时序，比较目标配置，验证 bitrate、采样点、FD 能力或接口状态变化时会重新配置，并校验 `ip link` 上下线命令。 |
| 插件 GoogleTest | `CanVirtualInterfaceTests` | `CanVirtualInterfaceTests.CanPluginReceiveOnVcan` | 在 Linux `vcan` 上验证 CAN 插件能建立接口并接收帧；运行依赖系统的虚拟 CAN 能力。 |
| 插件 GoogleTest | `FakeAdapterControlDynamicTest` | `FakeAdapterControlDynamicTest.AutomaticReplyProducesFeedback`<br>`FakeAdapterControlDynamicTest.DisabledRecordingKeepsObserver`<br>`FakeAdapterControlDynamicTest.ManualReplySuppressesFeedback`<br>`FakeAdapterControlDynamicTest.ObserverReceivesDecodedCommand`<br>`FakeAdapterControlDynamicTest.QueryReturnsControlForFakePlugin`<br>`FakeAdapterControlDynamicTest.QueryReturnsNullForNonFakeAdapter` | 验证动态加载的 Fake 插件可查询控制接口；观察者、记录开关、自动回包和手动回包模式行为正确。 |
| 插件 GoogleTest | `FdBrokerAuthenticationTests` | `FdBrokerAuthenticationTests.ConnectedPeerCannotStallPastHandshakeDeadline`<br>`FdBrokerAuthenticationTests.DirectPhaseRequiresExactChildPidAndRealUid`<br>`FdBrokerAuthenticationTests.EscalatedPhaseRequiresMatchingExecutableIdentity` | 验证 fd broker 的直接/提权两阶段身份校验，以及已连接对端无法无限期拖延握手。 |
| 插件 GoogleTest | `FdBrokerPrivilegeTests` | `FdBrokerPrivilegeTests.IdentityNumbersRejectInvalidTextWithoutThrowing`<br>`FdBrokerPrivilegeTests.RestartArgumentsPreserveNonceAndExecutableIdentity`<br>`FdBrokerPrivilegeTests.RestartArgumentsRejectIncompleteIdentity` | 验证 broker 重启参数中的 nonce、可执行文件身份和数值解析，并拒绝缺失或非法身份信息。 |
| 插件 GoogleTest | `PluginTests` | `PluginTests.MakeAdapterLoadsFakePluginExplicitly`<br>`PluginTests.MakeAdapterReusesAdapterByName`<br>`PluginTests.ManagerRollsBackInvalidDynamicFactoryResults`<br>`PluginTests.enum_plugin` | 验证动态插件枚举、Fake 插件显式加载、同名适配器复用，以及无效工厂结果的回滚。 |
| 插件 GoogleTest | `UsbSerialSenderTests` | `UsbSerialSenderTests.FirstWritesAreNotLimitedByRetryQueueCapacity`<br>`UsbSerialSenderTests.StopCancelsPendingRetries` | 验证 USB-Serial 首次发送不受重试队列容量限制，停止时会取消待重试发送。 |
| 核心 GoogleTest | `AdapterReceiveLifetimeTests` | `AdapterReceiveLifetimeTests.ExternalAdapterDeletionWaitsForRawCallbackCompletion`<br>`AdapterReceiveLifetimeTests.RawCallbackCannotDeleteOwningAdapterOrAnyOfItsBuses` | 验证原始帧回调期间禁止删除所属 adapter/bus，外部删除会等待回调执行完毕。 |
| 核心 GoogleTest | `AdapterRoutingTest` | `AdapterRoutingTest.DeliversUnregisteredFramesOutsideLegacyBuffers`<br>`AdapterRoutingTest.DropsUnknownBusWithoutCreatingLegacyTraffic`<br>`AdapterRoutingTest.RawCallbackPreservesMixedBatchAndRoutingContinues`<br>`AdapterRoutingTest.RoutesAreIsolatedByAdapterAndRawBus`<br>`AdapterRoutingTest.SerializesRegisteredCallbackDelivery`<br>`AdapterRoutingTest.SparseNegativeRawBusIndexKeepsRouteIdentity` | 验证注册路由的串行回调、未知总线丢弃、未注册帧旁路旧缓冲区、混合批次保留和 adapter/raw-bus 隔离。 |
| 核心 GoogleTest | `AdapterSoftSyncTests` | `AdapterSoftSyncTests.CommitDoesNotWaitForPhysicalCompletion`<br>`AdapterSoftSyncTests.CommitSubmitsAllCurrentDevicesAsOneBatch`<br>`AdapterSoftSyncTests.CommitUsesSynchronizedTransportHookOnlyForCommittedBatch`<br>`AdapterSoftSyncTests.ConcurrentCommitsSubmitIndependentBatches`<br>`AdapterSoftSyncTests.DeletingDeviceDiscardsItsQueuedPortSafely`<br>`AdapterSoftSyncTests.EmptyCommitIsHarmless`<br>`AdapterSoftSyncTests.ExplicitModeRetainsAndLeavingModeReleasesBacklog`<br>`AdapterSoftSyncTests.FirstCommitEntersSoftSyncMode`<br>`AdapterSoftSyncTests.MotorBatteryAndPmsUseRegistrationOrderInOneBatch`<br>`AdapterSoftSyncTests.OneDeviceRetainsNewestTenMessagesInFifoOrder`<br>`AdapterSoftSyncTests.ResponseWaitingApiDoesNotImplicitlyCommit`<br>`AdapterSoftSyncTests.SameBatterySerializesConcurrentCommandPublication`<br>`AdapterSoftSyncTests.SamePmsSerializesConcurrentCommandPublication` | 验证 adapter 软同步模式的进入/退出、批量提交顺序、空提交、积压队列、并发提交及等待 API 不会隐式提交。 |
| 核心 GoogleTest | `BatteryTests` | `BatteryTests.ClearFaultSendsPassiveCommandFrame`<br>`BatteryTests.DecodesActiveCommandsFrame`<br>`BatteryTests.DirectCallbacksDecodeLittleEndianFrames` | 验证电池直接回调的字节序解码、主动命令帧解析以及清故障生成正确的被动命令帧。 |
| 核心 GoogleTest | `BrakeTests` | `BrakeTests.BrakeDisableFailure`<br>`BrakeTests.BrakeDisableManualModeTimesOut`<br>`BrakeTests.BrakeDisableNoWait`<br>`BrakeTests.BrakeDisableSuccess`<br>`BrakeTests.BrakeDisableTimeout`<br>`BrakeTests.BrakeEnableFailure`<br>`BrakeTests.BrakeEnableNoWait`<br>`BrakeTests.BrakeEnableTimeout`<br>`BrakeTests.BrakeEnableUsesFormattedCommandAndAutomaticAck` | 验证刹车启停命令编码、成功/失败/超时/不等待和手动模式无回包场景。 |
| 核心 GoogleTest | `BusExternalDeviceTests` | `BusExternalDeviceTests.DetectExternalDeviceConsumesFreshTrafficAndSetsFlag`<br>`BusExternalDeviceTests.DetectExternalDeviceKeepsFlagForRegisteredExternalDevice`<br>`BusExternalDeviceTests.ExternalDeviceCreationConflictRollsBackBusFlag`<br>`BusExternalDeviceTests.GetBatteryMarksBusButAllowsMotorAccess`<br>`BusExternalDeviceTests.GetImuMarksBusButAllowsExplicitMotorAccess`<br>`BusExternalDeviceTests.GetPmsCachesWrapperAndMarksBus`<br>`BusExternalDeviceTests.ScanMotorsAbortsBeforeSendingDiscoveryQueries`<br>`BusExternalDeviceTests.ScanMotorsKeepsCurrentBusRepliesWhenOtherBusHasTraffic` | 验证电池/IMU/PMS 等外接设备的总线标志、缓存、创建冲突和扫描时对外接设备流量的处理。 |
| 核心 GoogleTest | `BusPortScanTests` | `BusPortScanTests.DestroyBusWaitsForScanBatchAcceptance`<br>`BusPortScanTests.DifferentBusScansQueueConcurrentlyAndSubmitSerially`<br>`BusPortScanTests.SameBusScansSerializeTheirMailboxConsumer`<br>`BusPortScanTests.ScanAcceptsOneValidReplyAndRejectsInvalidOrDuplicateReplies`<br>`BusPortScanTests.ScanCancelsWhenExistingDiscoveredMotorIsRetiring`<br>`BusPortScanTests.ScanClearsStaleFramesAndKeepsExistingNonResponders`<br>`BusPortScanTests.ScanConvertsParentRetirementDuringDiscoveryToCancellation`<br>`BusPortScanTests.ScanUsesCanFdRepliesAsHighestPriorityForCanDiscoveredIds`<br>`BusPortScanTests.UnknownFramesStayInTheirOwningBusMailbox` | 验证扫描与销毁并发、同/异总线扫描序列化、过期帧清理、有效/重复回复判定、CAN FD 优先级和取消。 |
| 核心 GoogleTest | `BusSoftSyncTests` | `BusSoftSyncTests.AdapterModeIsInheritedByNewBuses`<br>`BusSoftSyncTests.CommitOnlySubmitsCallingBus`<br>`BusSoftSyncTests.FirstCommitDoesNotRecoverDirectlySubmittedMessages`<br>`BusSoftSyncTests.LeavingModeReleasesOnlyCallingBusBacklog` | 验证 bus 级软同步只影响当前总线，模式继承和退出时仅释放本总线积压消息。 |
| 核心 GoogleTest | `CanIdTests` | `CanIdTests.GotoZeroFallsBackToLegacyControlWhenPositionQueryThrows`<br>`CanIdTests.GotoZeroUsesPositionReadThenSetPos`<br>`CanIdTests.MotorFrameFlagApiAppliesToSentMessages`<br>`CanIdTests.ResetZeroPosFailure`<br>`CanIdTests.ResetZeroPosNoWait`<br>`CanIdTests.ResetZeroPosSuccess`<br>`CanIdTests.ResetZeroPosTimeout`<br>`CanIdTests.ResetZeroPosUsesLegacyResetCommandOnly`<br>`CanIdTests.SetIdFailure`<br>`CanIdTests.SetIdMigrationPreparationFailureDoesNotSendFirmwareCommand`<br>`CanIdTests.SetIdMovesFeedbackRangeToNewId`<br>`CanIdTests.SetIdNoWait`<br>`CanIdTests.SetIdNoWaitLeavesMotorAtItsOriginalIndex`<br>`CanIdTests.SetIdPrefersTargetPendingStatusCallback`<br>`CanIdTests.SetIdPreservesDirectStatusCallback`<br>`CanIdTests.SetIdProducesFormattedCommandAndAcknowledges`<br>`CanIdTests.SetIdRouteConflictLeavesTheManagedMotorAtItsOriginalIndex`<br>`CanIdTests.SetIdSuccess`<br>`CanIdTests.SetIdTimeout`<br>`CanIdTests.SetPosProducesDecodedRadians`<br>`CanIdTests.SetPosSendsCentidegreePayloadAndReusesResetAck`<br>`CanIdTests.SetPosSendsCommandWithoutCanFdCapabilityProbe` | 验证 CAN ID 设置、迁移冲突与回滚、状态回调迁移、复位零位/设位置命令、帧标志与兼容路径。 |
| 核心 GoogleTest | `ControlParametersTests` | `ControlParametersTests.GetAccelerationTimeout`<br>`ControlParametersTests.IgnoredCanTimeoutWriteReturnsFalse`<br>`ControlParametersTests.ResetZeroPosFailure`<br>`ControlParametersTests.ResetZeroPosNoWait`<br>`ControlParametersTests.ResetZeroPosSuccess`<br>`ControlParametersTests.ResetZeroPosTimeout`<br>`ControlParametersTests.SetAccelerationAndGetAccelerationUseFakeSnapshot`<br>`ControlParametersTests.SetAccelerationFailure`<br>`ControlParametersTests.SetAccelerationNoWait`<br>`ControlParametersTests.SetAccelerationSuccess`<br>`ControlParametersTests.SetAccelerationTimeout`<br>`ControlParametersTests.SetCanTimeoutFailure`<br>`ControlParametersTests.SetCanTimeoutNoWait`<br>`ControlParametersTests.SetCanTimeoutSuccess`<br>`ControlParametersTests.SetCanTimeoutTimeout`<br>`ControlParametersTests.SetCommunicationModeAckWithWrongPayloadReturnsFalse`<br>`ControlParametersTests.SetCommunicationModeCanFdSuccess`<br>`ControlParametersTests.SetCommunicationModeClassicCanAckSucceeds`<br>`ControlParametersTests.SetCommunicationModeDoesNotChangeLocalCanFdFlag`<br>`ControlParametersTests.SetCommunicationModeIsRecordedAsRawParameterWrite`<br>`ControlParametersTests.SetCommunicationModeNoWait`<br>`ControlParametersTests.SetCommunicationModeRejectsInvalidMode`<br>`ControlParametersTests.SetCommunicationModeTimeout`<br>`ControlParametersTests.SetCurPIFailure`<br>`ControlParametersTests.SetCurPINoWait`<br>`ControlParametersTests.SetCurPISuccess`<br>`ControlParametersTests.SetCurPITimeout`<br>`ControlParametersTests.SetPosMinimumValidValue`<br>`ControlParametersTests.SetPosOutOfRangeThrows`<br>`ControlParametersTests.SetPosPDFailure`<br>`ControlParametersTests.SetPosPDNoWait`<br>`ControlParametersTests.SetPosPDSuccess`<br>`ControlParametersTests.SetPosPDTimeout`<br>`ControlParametersTests.SetSpdPIFailure`<br>`ControlParametersTests.SetSpdPINoWait`<br>`ControlParametersTests.SetSpdPISuccess`<br>`ControlParametersTests.SetSpdPITimeout` | 验证控制参数、CAN 超时、通信模式和加速度的边界校验、编码、成功/失败/超时/不等待响应及 Fake 快照更新。 |
| 核心 GoogleTest | `CurControlTests` | `CurControlTests.CurControlDecodesFormattedCommand`<br>`CurControlTests.CurControlNoResp1`<br>`CurControlTests.CurControlNoResp2`<br>`CurControlTests.CurControlNoResp3`<br>`CurControlTests.CurControlOverCurrent`<br>`CurControlTests.CurControlTest0`<br>`CurControlTests.CurControlTest1`<br>`CurControlTests.CurControlTest2`<br>`CurControlTests.CurControlTest3` | 验证电流控制在多种反馈类型、无回复、过流和命令编码条件下的行为。 |
| 核心 GoogleTest | `DriverManagerHotPathTest` | `DriverManagerHotPathTest.AdapterReceiveDoesNotWaitForManagerSlowPathLocks`<br>`DriverManagerHotPathTest.MotorControlDoesNotWaitForManagerSlowPathLocks`<br>`DriverManagerHotPathTest.RetiringDeviceDoesNotBlockIndependentObjects` | 验证电机控制、adapter 接收和对象退役不会被 DriverManager 慢路径锁阻塞。 |
| 核心 GoogleTest | `DriverManagerOwnershipTests` | `DriverManagerOwnershipTests.ConcurrentCreationDeduplicatesEveryManagedObjectType` | 验证多线程创建 adapter/bus/设备时每种托管对象均被去重。 |
| 核心 GoogleTest | `DriverManagerTest` | `DriverManagerTest.AdapterCascadeWaitsForAnIndependentlyDeletingBusAndDestroysBottomUp`<br>`DriverManagerTest.AdapterDeletionHidesDescendantsAndCreationWaitsForReplacement`<br>`DriverManagerTest.AdapterDestructionClearsStatusStateBeforeAddressReuse`<br>`DriverManagerTest.AdapterOnMessageDoesNotPlaceUnregisteredFrameInLegacyBuffers`<br>`DriverManagerTest.AdapterOnMessageDropsUnknownBusWithoutCreatingTraffic`<br>`DriverManagerTest.AdapterOnMessageRoutesRegisteredFramesSerially`<br>`DriverManagerTest.BoundWriterSanitizesFlagsAndKeepsBusIdentity`<br>`DriverManagerTest.BusAndAdapterCascadeDeleteChildrenAndPermitFreshIdentity`<br>`DriverManagerTest.BusDeletionHidesSnapshotsAndCreationWaitsForReplacement`<br>`DriverManagerTest.BusDeletionUnregistersKnownBusIndexAndAllowsCleanRecreation`<br>`DriverManagerTest.CallbackCanDeleteAnUnrelatedSiblingDevice`<br>`DriverManagerTest.CallbackExceptionDoesNotEscapeDispatch`<br>`DriverManagerTest.CrossCallbackDeletionCycleIsRejectedWithoutDeadlock`<br>`DriverManagerTest.DeletionDrainsDirectBatteryCallbacksWithoutHoldingGlobalLock`<br>`DriverManagerTest.DeviceDeletionHidesSnapshotsAndCreationWaitsForReplacement`<br>`DriverManagerTest.DeviceDeletionWaitsForDirectPublicMethod`<br>`DriverManagerTest.DirectBatteryRouteOwnsEveryReportId`<br>`DriverManagerTest.DirectBatteryRouteRejectsExternalCallbackInstallation`<br>`DriverManagerTest.DuplicateReceiveIdIsRejectedWithoutChangingExistingRoute`<br>`DriverManagerTest.FactoryAndRouteConflictsRollBackCreation`<br>`DriverManagerTest.FailedBusPublicationUnregistersItsKnownBusIndex`<br>`DriverManagerTest.FailedMotorIndexMigrationKeepsSourceAndTargetPendingCallbacks`<br>`DriverManagerTest.IndividualDeviceDeletionUnregistersRoutesAndAllowsRecreation`<br>`DriverManagerTest.MotorCreationPublishesLatestStatusConfigurationAndPendingCallback`<br>`DriverManagerTest.MotorIndexMigrationPrefersTargetPendingStatusCallback`<br>`DriverManagerTest.MotorIndexMigrationPreservesDirectMotorStatusCallback`<br>`DriverManagerTest.MotorIndexMigrationRejectsConflictsWithoutMutatingIndexesOrRoutes`<br>`DriverManagerTest.MotorIndexMigrationRollsBackWhenCurrentRangeMoveFails`<br>`DriverManagerTest.NewlyPublishedBusInheritsConcurrentAdapterDefaultMode`<br>`DriverManagerTest.ParentCascadeWaitsForAnIndependentlyDeletingChild`<br>`DriverManagerTest.ParentDeletionWaitsForPendingBusOrDevicePublicationAndRollsBack`<br>`DriverManagerTest.PendingStatusCallbackAppliesBeforeMotorCreation`<br>`DriverManagerTest.RejectsUnknownMismatchedAndCallbackOwnedDeletion`<br>`DriverManagerTest.RouteRegistrationIsRejectedWhileDeviceDeletionIsInFlight`<br>`DriverManagerTest.StatusCallbackCanReconfigureItsMotor`<br>`DriverManagerTest.StatusConfigurationIsRejectedWhileAdapterDeletionIsInFlight` | 验证对象工厂、路由登记、回调重入与异常、状态配置、索引迁移、并发创建/删除、级联销毁和回滚的一致性。 |
| 核心 GoogleTest | `DriverManagerTests` | `DriverManagerTests.ReceiveUniqueIdPreservesSignedBusBitsWithoutSignedShift`<br>`DriverManagerTests.SingletonAndRawPointerAliasesArePublic` | 验证 DriverManager 单例/原始指针别名接口，以及带符号总线位构成唯一 ID 的正确性。 |
| 核心 GoogleTest | `EthercatBaseHandleTests` | `EthercatBaseHandleTests.AcceptsKnownProcessDataMapWithinSafetyLimit`<br>`EthercatBaseHandleTests.AcceptsProcessDataMapAboveLegacyFourKilobyteBuffer`<br>`EthercatBaseHandleTests.AcceptsProcessDataMapExactlyAtSafetyLimit`<br>`EthercatBaseHandleTests.AsynchronousBatchesContinueSharingOutputFrames`<br>`EthercatBaseHandleTests.CanFd3BusDecodeSkipsZeroLengthBlocksAndKeepsDuplicates`<br>`EthercatBaseHandleTests.CanFd3BusPacksEightSlotsPerBusAndSplitsOverflow`<br>`EthercatBaseHandleTests.CanFd3BusRejectsFourthBus`<br>`EthercatBaseHandleTests.CanFd8Bus10SlotsDecodesEightiethSlot`<br>`EthercatBaseHandleTests.CanFd8Bus10SlotsPacksTenSlotsPerBusAndSplitsOverflow`<br>`EthercatBaseHandleTests.CanFd8BusDecodeSkipsZeroLengthBlocksAndKeepsDuplicates`<br>`EthercatBaseHandleTests.CanFd8BusPacksEightSlotsPerBusAndSplitsOverflow`<br>`EthercatBaseHandleTests.ClassicCan2BusDecodeKeepsDuplicates`<br>`EthercatBaseHandleTests.ClassicCan8BusStillUsesThreeSlotsPerBus`<br>`EthercatBaseHandleTests.ClassicCanScanBurstKeepsOnlyTheNewestFrameUnderHighBacklog`<br>`EthercatBaseHandleTests.HighBacklogKeepsOnlyTheNewestQueuedFrame`<br>`EthercatBaseHandleTests.HighBacklogThresholdAllowsThreeQueuedFrames`<br>`EthercatBaseHandleTests.OutputPdoClassificationRequiresAnExactSupportedSize`<br>`EthercatBaseHandleTests.PackPropagatesFrameFlagsToEthercatSlots`<br>`EthercatBaseHandleTests.PremapCapacityRejectsMissingPdoDescription`<br>`EthercatBaseHandleTests.PremapCapacityUsesSyncManagersInsteadOfFinalByteCounters`<br>`EthercatBaseHandleTests.PrepareNextFrameAggregatesQueuedSingleMessagesIntoOneFrame`<br>`EthercatBaseHandleTests.RejectsEmptyOrOversizedMappedResult`<br>`EthercatBaseHandleTests.RejectsProcessDataMapAboveSafetyLimitBeforeMapping`<br>`EthercatBaseHandleTests.RejectsUnknownSyncManagerType`<br>`EthercatBaseHandleTests.SameLocalBusOnDifferentSlavesUsesIndependentGenerations`<br>`EthercatBaseHandleTests.SynchronizedBatchesUseExclusiveOutputFrames`<br>`EthercatBaseHandleTests.SynchronizedBoundariesAreScopedToTheirBus` | 验证 EtherCAT PDO 映射边界、CAN/CAN FD 槽位打包解包、帧标志、积压阈值、同步边界和多从站隔离。 |
| 核心 GoogleTest | `EthercatLoopPeriodTests` | `EthercatLoopPeriodTests.SoemAdapterDefaultsToOneKilohertz` | 验证 SOEM EtherCAT 适配器默认循环频率为 1 kHz。 |
| 核心 GoogleTest | `EthercatWindowsDiscoveryTests` | `EthercatWindowsDiscoveryTests.SeededMotorsDriveDiscoveryFlags` | 验证 Windows EtherCAT 发现使用预置电机信息驱动发现标志。 |
| 核心 GoogleTest | `FakeAdapterBaseTests` | `FakeAdapterBaseTests.CanDisableMedianAndIndividualLimitFilters`<br>`FakeAdapterBaseTests.DisablesStatusFiltersByDefault`<br>`FakeAdapterBaseTests.EnablesMedianAndLimitFiltersIndependently`<br>`FakeAdapterBaseTests.FiltersFeedbackSpikeBeforeCachingAndCallingStatusCallback`<br>`FakeAdapterBaseTests.InjectedFeedbackUpdatesAdapterStatusCache`<br>`FakeAdapterBaseTests.KeepsFieldsAbsentFromFeedbackAsNan`<br>`FakeAdapterBaseTests.ManualModeRecordsCommandsWithoutAutoReply`<br>`FakeAdapterBaseTests.PreservesSpeedFilterHistoryWhenStatusExpires`<br>`FakeAdapterBaseTests.RejectsOverLimitFeedbackWithoutUsingItAsTheNextReference`<br>`FakeAdapterBaseTests.UpdatingLifeCycleKeepsMedianSamples`<br>`FakeAdapterBaseTests.UpdatingLimitFilterKeepsMedianSamples`<br>`FakeAdapterBaseTests.UpdatingMedianFilterKeepsLimitReference` | 验证 Fake adapter 的状态缓存、反馈限幅/中值滤波、生命周期更新和手动模式记录。 |
| 核心 GoogleTest | `FakeAdapterControlStaticTest` | `FakeAdapterControlStaticTest.DisabledRecordingKeepsObserverAndStopsHistory`<br>`FakeAdapterControlStaticTest.ObserverReceivesDecodedCommand`<br>`FakeAdapterControlStaticTest.QueryReturnsNullForNonFakeAdapter` | 验证静态模式 Fake 控制接口对非 Fake 返回空、观察者接收命令、关闭记录不影响观察者。 |
| 核心 GoogleTest | `FakeAdapterTests` | `FakeAdapterTests.AutoCreateMotorInitializesPositionToZeroBeforeSpeedControl`<br>`FakeAdapterTests.AutoCreateMotorIsDisabledByDefault`<br>`FakeAdapterTests.AutoCreateUsesEC_A4310_P2RangesForFirstCommandDecoding`<br>`FakeAdapterTests.AutomaticWritePolicyCanIgnoreParameterAck`<br>`FakeAdapterTests.ClearDecodedCommandObserverStopsNotifications`<br>`FakeAdapterTests.CommandRecordsAreNotPrunedByTimeWindow`<br>`FakeAdapterTests.ConcurrentCommandsHistoryReadsAndObserverReentryAreSafe`<br>`FakeAdapterTests.CurControlAcceleratesBasedOnCurrent`<br>`FakeAdapterTests.CurControlContinuesPriorSimulatedMotion`<br>`FakeAdapterTests.CurControlNegativeCurrentDecelerates`<br>`FakeAdapterTests.DecodedCommandObserverReceivesEachRecordOnce`<br>`FakeAdapterTests.DisabledPositionErrorUsesDeterministicIncrement`<br>`FakeAdapterTests.EnableAutoCreateMotorCreatesSnapshotAndReplies`<br>`FakeAdapterTests.EnableAutoCreateMotorWithManualReplyDoesNotEmitFeedback`<br>`FakeAdapterTests.GetBusesIncludesCreatedAndSeededBuses`<br>`FakeAdapterTests.GetParameterReadsSimulatedRunningState`<br>`FakeAdapterTests.InjectionRoutesRegisteredUnknownAndUnknownBusFrames`<br>`FakeAdapterTests.ManagerOwnsControlAndDeletesTheCompleteFakeSubtree`<br>`FakeAdapterTests.ManualReplyModeRecordsCommandsWithoutChangingSnapshots`<br>`FakeAdapterTests.NegativeBusIndexStatusUsesStableUniqueKey`<br>`FakeAdapterTests.ObserverDoesNotChangeNormalRecordingAndReply`<br>`FakeAdapterTests.PVTDecodeUsesSeededMotorRanges`<br>`FakeAdapterTests.ParameterReadFirstAutoCreatesAndUsesGeneratedRanges`<br>`FakeAdapterTests.PosControlDoesNotIntegrateSpeedLimitAfterReachingTarget`<br>`FakeAdapterTests.RunningParameterReadAdvancesPositionAfterSpeedCommand`<br>`FakeAdapterTests.SeedMotorFromModelCreatesSnapshotAndDecodesSpdControlCommand`<br>`FakeAdapterTests.SpdControlAdvancesPositionWithinErrorBound`<br>`FakeAdapterTests.SpdControlSetsSpeedDirectly`<br>`FakeAdapterTests.TorControlAdvancesPositionWithinErrorBound` | 验证 Fake 电机建模、自动创建、反馈注入、命令记录、参数读取、PVT/位置/速度/电流/扭矩模拟及并发安全。 |
| 核心 GoogleTest | `FakeProtocolTests` | `FakeProtocolTests.ResetZeroPosRecordUsesSourceMotorId`<br>`FakeProtocolTests.SetIdRecordUsesSourceMotorId`<br>`FakeProtocolTests.SetPosRecordUsesSourceMotorId` | 验证 Fake 协议对设置 ID、设置位置和回零命令记录中的源电机 ID。 |
| 核心 GoogleTest | `ImuTests` | `ImuTests.ExposesNativeAndJsUpdateIntervals`<br>`ImuTests.IgnoresShortFramesAndExpiresStaleGroups`<br>`ImuTests.UpdatePassCoalescesCallbackAndDecodesYis130Frames` | 验证 IMU 帧解码、短帧处理、状态组过期及原生/JavaScript 更新间隔暴露。 |
| 核心 GoogleTest | `LogWriterTests` | `LogWriterTests.IsolatesWorkerFailureToOneWriter`<br>`LogWriterTests.PreservesExistingFileAndIncrementsTimestampCandidate`<br>`LogWriterTests.SerializesConcurrentProducersWithoutLosingRows`<br>`LogWriterTests.WritesHeaderAndTypedCsvRow`<br>`LogWriterTests.WritesMultipleFramesAndFlushesOnDestruction` | 验证 CSV 表头与行、文件名冲突处理、析构 flush、并发生产者和单个 writer 失败隔离。 |
| 核心 GoogleTest | `MotorEncodingTests` | `MotorEncodingTests.ConcurrentSpeedControlProducesStableFloatBytes`<br>`MotorEncodingTests.FloatBitsPreservesProtocolObjectRepresentation` | 验证电机浮点编码保持对象表示，并发速度控制的字节编码稳定。 |
| 核心 GoogleTest | `MotorLogTransportTests` | `MotorLogTransportTests.CommandPortRetainsNewestRecords`<br>`MotorLogTransportTests.StatusPortRetainsNewestRecords`<br>`MotorLogTransportTests.UsesTriviallyCopyableFixedRecords` | 验证电机日志传输帧的编码、分段/解码和异常输入处理。 |
| 核心 GoogleTest | `MotorLoggingTests` | `MotorLoggingTests.DisablesImmediatelyWhenAllReconstructionAttemptsFail`<br>`MotorLoggingTests.ExplicitDisableCleansUpBeforeReportingFlushFailure`<br>`MotorLoggingTests.LogsSevenControlTypesAndSkipsManagementCommands`<br>`MotorLoggingTests.RebuildsBothWritersAfterAnAsynchronousFailure`<br>`MotorLoggingTests.RecoveryOfOneMotorDoesNotAffectAnotherMotor`<br>`MotorLoggingTests.SameBaseIsNoOpAndDifferentBaseSwitchesThePair`<br>`MotorLoggingTests.StatusLogFailureDoesNotPreventUserCallback`<br>`MotorLoggingTests.StatusLoggingCoexistsWithUserCallbackAcrossDisable` | 验证电机日志配置、读写、CSV 记录、状态与多帧日志行为。 |
| 核心 GoogleTest | `MotorTestFixture` | `MotorTestFixture.BatteryCallbackRunsSynchronouslyForEveryValidFrame`<br>`MotorTestFixture.BatteryDeletionUnregistersDirectReportRoutes`<br>`MotorTestFixture.BatteryDirectRouteDecodesEveryReportIdAndAllowsCallbackReentry`<br>`MotorTestFixture.BatteryDirectRouteRejectsMalformedFramesAndIsolatesIndices`<br>`MotorTestFixture.BatteryInitialAllZeroErrorFrameStillInvokesCallback`<br>`MotorTestFixture.BatteryStatusGroupsExpireIndependentlyAndReportCommTimeout`<br>`MotorTestFixture.ImuCallbackRunsOnReceiveThreadWithoutPolling`<br>`MotorTestFixture.ImuDeletionUnregistersDirectReportRoutes`<br>`MotorTestFixture.ImuDirectRouteRejectsMalformedFramesAndIsolatesIndices`<br>`MotorTestFixture.ImuDirectRouteUpdatesAllGroupsAndAllowsCallbackReentry`<br>`MotorTestFixture.ImuStatusGroupsExpireIndependently`<br>`MotorTestFixture.PmsCallbackCoalescesFramesOnReceiveThread`<br>`MotorTestFixture.PmsCompleteStatusExpiresWithoutTimeoutCallback`<br>`MotorTestFixture.PmsDeletionUnregistersDirectReportRoutes`<br>`MotorTestFixture.PmsDirectRouteRejectsMalformedFramesAndAllowsCallbackReentry` | 验证电池、IMU、PMS 的直接路由、同步回调、分组状态过期、畸形帧隔离、重入与删除后取消注册。 |
| 核心 GoogleTest | `OperationRegistryTest` | `OperationRegistryTest.AddressReusePublishesANewGeneration`<br>`OperationRegistryTest.ConcurrentRetireDrainAndAddressReusePublishNewGeneration`<br>`OperationRegistryTest.HazardCapacityExhaustionHasDistinctDiagnostic`<br>`OperationRegistryTest.RepeatedChurnReclaimsEntriesAndKeepsLookupBounded`<br>`OperationRegistryTest.RetiredSidecarRemainsStableWithoutDereferencingObject` | 验证操作登记表在对象退役、地址复用、反复 churn、容量耗尽和并发 drain 下的代际与内存安全。 |
| 核心 GoogleTest | `PVTControlTests` | `PVTControlTests.PVTControlAutomaticFeedbackUpdatesSnapshot`<br>`PVTControlTests.PVTControlClampsIntoDecodedCommandRecord`<br>`PVTControlTests.PVTControlManualModeReturnsNoResponse`<br>`PVTControlTests.PVTControlOverCurrentUsesSnapshotError` | 验证 PVT 控制的范围钳制、手动模式无回复、自动反馈和过流错误。 |
| 核心 GoogleTest | `PVTRangeTests` | `PVTRangeTests.ConcurrentRangeReadsObserveCoherentSnapshots`<br>`PVTRangeTests.SetDriverPVTRanges`<br>`PVTRangeTests.SetPVTCurRangeUpdatesSnapshotAndCommandRecord`<br>`PVTRangeTests.SetPVTKdRangeFailure`<br>`PVTRangeTests.SetPVTKdRangeNoWait`<br>`PVTRangeTests.SetPVTKdRangeSuccess`<br>`PVTRangeTests.SetPVTKdRangeTimeout`<br>`PVTRangeTests.SetPVTKpRangeFailure`<br>`PVTRangeTests.SetPVTKpRangeNoWait`<br>`PVTRangeTests.SetPVTKpRangeSuccess`<br>`PVTRangeTests.SetPVTKpRangeTimeout`<br>`PVTRangeTests.SetPVTPosRangeCanBeIgnored`<br>`PVTRangeTests.SetPVTPosRangeFailure`<br>`PVTRangeTests.SetPVTPosRangeNoWait`<br>`PVTRangeTests.SetPVTPosRangeSuccess`<br>`PVTRangeTests.SetPVTPosRangeTimeout`<br>`PVTRangeTests.SetPVTSpdRangeFailure`<br>`PVTRangeTests.SetPVTSpdRangeNoWait`<br>`PVTRangeTests.SetPVTSpdRangeSuccess`<br>`PVTRangeTests.SetPVTSpdRangeTimeout`<br>`PVTRangeTests.SetPVTTorRangeFailure`<br>`PVTRangeTests.SetPVTTorRangeNoWait`<br>`PVTRangeTests.SetPVTTorRangeSuccess`<br>`PVTRangeTests.SetPVTTorRangeTimeout` | 验证 PVT 各位置/速度/扭矩/Kp/Kd 范围设置的快照、忽略项、成功/失败/超时/不等待及并发读取一致性。 |
| 核心 GoogleTest | `PlatformSyncTest` | `PlatformSyncTest.LinuxMutexesUsePriorityInheritance`<br>`PlatformSyncTest.MutexSerializesCompetingThreads`<br>`PlatformSyncTest.RecursiveMutexAllowsNestedLocking` | 验证 Linux 优先级继承 mutex、普通互斥竞争和递归互斥行为。 |
| 核心 GoogleTest | `PluginOwnershipTests` | `PluginOwnershipTests.AdapterCascadeRetiresRoutesBeforeStoppingAndJoiningWorker`<br>`PluginOwnershipTests.ManagerAdoptsExactlyOnceAndRollsBackInvalidFactoryResults` | 验证插件工厂对象只被管理器接管一次、无效结果回滚，以及 adapter 级联退役路由和工作线程。 |
| 核心 GoogleTest | `PmsTests` | `PmsTests.AggregatesCompleteStatusAndCallsBackAfterAllFramesUpdate`<br>`PmsTests.IgnoresShortFramesAndInvalidatesWholeStatusWhenOneFrameExpires`<br>`PmsTests.SendCommandEncodesExtendedChannelControlFrame`<br>`PmsTests.SendCommandRejectsConflictingChannelActions` | 验证 PMS 多帧状态聚合、短帧/超时失效、扩展通道控制帧和冲突动作拒绝。 |
| 核心 GoogleTest | `PortConcurrencyTests` | `PortConcurrencyTests.ClearDuringPublicationNeverReturnsTornMessages`<br>`PortConcurrencyTests.ConcurrentOverwriteNeverReturnsTornOrDuplicateMessages` | 验证并发覆盖和清空期间不会读到撕裂或重复消息。 |
| 核心 GoogleTest | `PortLivenessRegressionTests` | `PortLivenessRegressionTests.ClearCompletesWhenProducerPausesAfterOddHeadStore`<br>`PortLivenessRegressionTests.HeadSnapshotSurvivesVersionAndActiveIndexAba`<br>`PortLivenessRegressionTests.PopCompletesWhenProducerPausesAfterOddSlotStore`<br>`PortLivenessRegressionTests.PopResynchronizesWithinSameCallWhenSlotLeadsHead`<br>`PortLivenessRegressionTests.SlotSnapshotSurvivesVersionAndActiveIndexAba` | 回归验证 producer 在奇数版本写入暂停、槽位领先或 ABA 情况下，`Pop`/`Clear` 仍能完成。 |
| 核心 GoogleTest | `PortProgressTests` | `PortProgressTests.ContinuousWriterRetainsNewestWindowWithoutReaderProgress`<br>`PortProgressTests.CrossesSequenceLowWordRolloverWithoutLosingFifoOrder`<br>`PortProgressTests.EveryValidationRetryHasMeasuredProducerProgress`<br>`PortProgressTests.PublicationCompletesBeforeArbitraryCallbackReturns`<br>`PortProgressTests.SuspendedReaderNeverBlocksRepeatedWriterWraps` | 验证无锁 Port 的 producer/consumer 进度、回调发布顺序、持续写入和序列号低位回绕。 |
| 核心 GoogleTest | `PortTests` | `PortTests.EmptyTracksConsumerVisibleMessages`<br>`PortTests.InvokesTypedCallbackSynchronouslyWithoutConsumingMessage`<br>`PortTests.OverwritesOldestAndRetainsNewestCapacityMessages`<br>`PortTests.PreservesCustomMotorPackMessageFields`<br>`PortTests.PreservesExactCapacityInFifoOrder`<br>`PortTests.PreservesFifoOrderAndClearsUnreadMessages`<br>`PortTests.ProducerAndConsumerHotStateUseDistinctCacheLines`<br>`PortTests.RetainsNewestMessagesAcrossMultipleFullWraps`<br>`PortTests.StoresImmutableCanIdAndStartsEmpty` | 验证 Port 的 CAN ID、缓存行布局、FIFO、容量覆盖、清空、消息字段与同步回调语义。 |
| 核心 GoogleTest | `PosControlTests` | `PosControlTests.PosControlDecodesPayloadAndReturnsAutoFeedback`<br>`PosControlTests.PosControlFeedbackMismatchStillThrows`<br>`PosControlTests.PosControlNoResp1`<br>`PosControlTests.PosControlNoResp2`<br>`PosControlTests.PosControlNoResp3`<br>`PosControlTests.PosControlTest0`<br>`PosControlTests.PosControlTest1`<br>`PosControlTests.PosControlTest2`<br>`PosControlTests.PosControlTest3` | 验证位置控制多反馈类型、无回复、过流、命令编码和自动反馈快照更新。 |
| 核心 GoogleTest | `RelayFrameTest` | `RelayFrameTest.EncodesAndDecodesMultipleRecords`<br>`RelayFrameTest.EncodesAndDecodesSingleRecord`<br>`RelayFrameTest.EncodesEmptyFrame`<br>`RelayFrameTest.RejectsInvalidByteLength`<br>`RelayFrameTest.RejectsLenGreaterThan8`<br>`RelayFrameTest.RejectsUnsupportedType`<br>`RelayFrameTest.RejectsWrongMagic`<br>`RelayFrameTest.SplitsRecordsAtCount255` | 验证 Relay EMR1 帧的空帧、单/多记录编码解码、255 条拆分，以及 magic/type/长度校验。 |
| 核心 GoogleTest | `RelayWsAdapterTests` | `RelayWsAdapterTests.CascadeDeletionDrainsReceiveCallbackBeforeJoiningWorker`<br>`RelayWsAdapterTests.ConstructorWaitsForDelayedWebSocketConnection`<br>`RelayWsAdapterTests.SendsEmptyFrameAtFixedPeriodWhenIdle` | 验证 RelayWs 连接等待、空闲定时发送空帧、删除时等待接收回调并停止 worker。 |
| 核心 GoogleTest | `RelayWsClientQueueTests` | `RelayWsClientQueueTests.OversizedFrameDoesNotEvictRetainedFrames` | 验证 RelayWs 客户端超大帧不会挤掉已保留队列帧。 |
| 核心 GoogleTest | `RelayWsQueueTests` | `RelayWsQueueTests.FailedBatchReinsertionDropsOldestAcrossCombinedQueue`<br>`RelayWsQueueTests.GenerationAwareFramesSplitAtRecordLimit`<br>`RelayWsQueueTests.SameBusGenerationsUseSeparateEmr1Frames`<br>`RelayWsQueueTests.TaggedFailedBatchReinsertionPreservesGenerations` | 验证 RelayWs 队列失败批次回插、跨代/同总线隔离、记录上限拆分和最旧帧淘汰。 |
| 核心 GoogleTest | `RelayWsUrlTest` | `RelayWsUrlTest.ExtractsQueryValue`<br>`RelayWsUrlTest.ExtractsSessionAndBusCountFromJsonResponse`<br>`RelayWsUrlTest.ParsesHttpUrlWithPortAndQuery`<br>`RelayWsUrlTest.ParsesHttpsUrlWithDefaultPort`<br>`RelayWsUrlTest.ParsesUrlWithoutPath`<br>`RelayWsUrlTest.RejectsMalformedUrl`<br>`RelayWsUrlTest.UrlDecodesQueryValue` | 验证 Relay URL 的协议、端口、路径和查询解析/解码，以及启动响应中 session/bus 数读取。 |
| 核心 GoogleTest | `ResponseWaiterTests` | `ResponseWaiterTests.CheckerOrWriterExceptionCancelsOnlyThatWaiter`<br>`ResponseWaiterTests.DestroyMotorCancelsAndWakesPendingWaiter`<br>`ResponseWaiterTests.DiscoveryProtocolOverridesOtherFlagsBeforeRangeInitialization`<br>`ResponseWaiterTests.ExplicitCanFdConstructorOverloadsPreserveOtherFrameFlags`<br>`ResponseWaiterTests.ExplicitCanFdFirmwareOverloadUsesProtocolBeforeInitialization`<br>`ResponseWaiterTests.ExplicitCanFdKnownModelOverloadUpdatesReusedMotor`<br>`ResponseWaiterTests.ExplicitCanFdRangesOverloadPreservesRanges`<br>`ResponseWaiterTests.FailedFirmwareInitializationRollsBackRouteAndPendingObject`<br>`ResponseWaiterTests.FirmwareInitializationRegistersRouteBeforeSynchronousReplies`<br>`ResponseWaiterTests.InitializerFailureWaitsForInFlightRouteCallback`<br>`ResponseWaiterTests.MatchingStatusCompletesAllTypedWaitersAndUpdatesStatus`<br>`ResponseWaiterTests.MatchingWaiterCompletesBeforeBlockingStatusCallback`<br>`ResponseWaiterTests.RegistersBeforeSynchronousFeedback`<br>`ResponseWaiterTests.ResetMotorsIdMigratesTheOnlyRemainingMotorToIdOne`<br>`ResponseWaiterTests.ResetMotorsIdMigrationFailurePreservesSurvivorAndReportsFailure`<br>`ResponseWaiterTests.ResetMotorsIdRejectsCreationUntilRetirementCompletes`<br>`ResponseWaiterTests.ResetMotorsIdRetiresEveryMotorExceptExistingIdOne`<br>`ResponseWaiterTests.ResetMotorsIdTimeoutLeavesManagedMotorsUnchanged`<br>`ResponseWaiterTests.RetiredMotorRejectsEveryPublicStatusConfigurationSetter`<br>`ResponseWaiterTests.SameMotorTransactionsKeepResponsesIsolated`<br>`ResponseWaiterTests.SharedSystemResponseBroadcastsToEveryMotorWaiter`<br>`ResponseWaiterTests.SharedSystemResponseRemainsInBusUnknownMailbox`<br>`ResponseWaiterTests.SpuriousWakeDoesNotCompleteWaiterPredicate`<br>`ResponseWaiterTests.TimeoutUnregistersWaiterBeforeLateFrameCanWrite` | 验证电机命令应答等待的匹配、超时、取消、并发、重入及对象删除交互。 |
| 核心 GoogleTest | `RuntimeStoreTest` | `RuntimeStoreTest.ExhaustedDeferredDeletionRetainsLeaseAndCanBeRetried`<br>`RuntimeStoreTest.FailedFinalDeletionKeepsAdapterHandleRetriable`<br>`RuntimeStoreTest.FakeAdapterHandlesShareManagerObjectUntilLastLeaseDisposes`<br>`RuntimeStoreTest.FakeCreationRejectsAnExistingNonFakeManagerAdapter`<br>`RuntimeStoreTest.FinalDisposalInvalidatesDescendantsBeforeManagerDeletion`<br>`RuntimeStoreTest.LastLeaseDestroysSharedFakeAdapterRegardlessOfDisposalOrder`<br>`RuntimeStoreTest.NormalAdapterHandlesReuseTheManagerOwnedAdapter` | 验证 WASM 运行时对象句柄、adapter 销毁边界、延迟删除、引用租约和失败重试恢复。 |
| 核心 GoogleTest | `SpdControlTests` | `SpdControlTests.SpdControlDecodesFormattedCommandAndUpdatesSnapshot`<br>`SpdControlTests.SpdControlNoResp1`<br>`SpdControlTests.SpdControlNoResp2`<br>`SpdControlTests.SpdControlNoResp3`<br>`SpdControlTests.SpdControlOverCurrent`<br>`SpdControlTests.SpdControlTest0`<br>`SpdControlTests.SpdControlTest1`<br>`SpdControlTests.SpdControlTest2`<br>`SpdControlTests.SpdControlTest3` | 验证速度控制多反馈类型、无回复、过流、命令编码和 Fake 状态更新。 |
| 核心 GoogleTest | `StatusLifecycleTests` | `StatusLifecycleTests.DefaultLifeCycleKeepsStatusAvailable`<br>`StatusLifecycleTests.FiniteLifeCycleClampsExistingDefaultStatus`<br>`StatusLifecycleTests.InjectedFeedbackParticipatesInLifecycleExpiry`<br>`StatusLifecycleTests.NewFeedbackResetsLifeCycleToMaxValue`<br>`StatusLifecycleTests.NonPositiveDeductionDoesNotConsumeLifeCycle`<br>`StatusLifecycleTests.SnapshotReadsDoNotConsumeLifecycle` | 验证电机状态生命周期、超时/刷新、回调设置和状态快照一致性。 |
| 核心 GoogleTest | `StopTests` | `StopTests.StopDecodesModeAndCurrent`<br>`StopTests.StopNoResp1`<br>`StopTests.StopNoResp2`<br>`StopTests.StopNoResp3`<br>`StopTests.StopOverCurrent`<br>`StopTests.StopTest0`<br>`StopTests.StopTest1`<br>`StopTests.StopTest2`<br>`StopTests.StopTest3`<br>`StopTests.StopUsesRequestedMode` | 验证停止控制多反馈类型、无回复、过流、停止模式与命令编码。 |
| 核心 GoogleTest | `ThreadPriorityHelperTests` | `ThreadPriorityHelperTests.ExecutableFailsClosedWithoutCapability`<br>`ThreadPriorityHelperTests.ExecutableRejectsMalformedAndExternalTargets`<br>`ThreadPriorityHelperTests.KeepsOpenedInodeWhenPathIsReplaced`<br>`ThreadPriorityHelperTests.ParsesOnlyExactPositiveDecimalArguments`<br>`ThreadPriorityHelperTests.PreservesVerifiedPathForCapabilityGrant`<br>`ThreadPriorityHelperTests.RejectsPriorityOutsideSystemFifoRange`<br>`ThreadPriorityHelperTests.RequiresDirectParentAndThreadMembership`<br>`ThreadPriorityHelperTests.SerializesConcurrentRequests` | 验证 helper 仅接受严格优先级参数、父子进程/线程成员关系、能力失败关闭、路径替换防护与并发序列化。 |
| 核心 GoogleTest | `TorControlTests` | `TorControlTests.TorControlDecodesFormattedCommand`<br>`TorControlTests.TorControlFeedbackType3DecodesTorqueAndUpdatesSnapshot`<br>`TorControlTests.TorControlNoResp1`<br>`TorControlTests.TorControlNoResp2`<br>`TorControlTests.TorControlNoResp3`<br>`TorControlTests.TorControlOverCurrent`<br>`TorControlTests.TorControlTest0`<br>`TorControlTests.TorControlTest1`<br>`TorControlTests.TorControlTest2`<br>`TorControlTests.TorControlTest3` | 验证扭矩控制多反馈类型、无回复、过流、命令编码和扭矩反馈解码。 |
<!-- GTEST-REGISTERED-ITEMS-END -->

## 2. 已移除的 CMake、配置和策略测试

本次重构已删除本节全部入口和资产；它们不再在 configure 阶段或 `ctest` 中运行。以下清单保留被删除项目的名称与原覆盖目标，便于追溯本次变更的精确范围。

### 2.1 已移除的配置阶段编译断言

原先这些断言在启用 `ENCOS_BUILD_TESTS` 的 **CMake configure 阶段**执行：

| 断言 | 检查内容 |
| --- | --- |
| `ENCOS_PORT_VALID_CAPACITY_COMPILES` | `encos::Port<3>` 必须可编译 |
| 4 个 `encos_expect_port_compile_failure` 检查 | `Port<0>`、`Port<1>`、`Port<2>` 及携带非平凡消息类型的 `Port` 必须编译失败 |
| `ENCOS_DRIVER_API_FUNCTIONS_COMPILE` | 兼容 API 必须是可取地址函数，而不是宏，且函数签名必须匹配 |

### 2.2 已移除的 Python CMake 契约测试

| CTest 名称 | 脚本 | 内部测试数 | 覆盖重点 |
| --- | --- | ---: | --- |
| `EncosCMakeConfigurePolicies` | `tests/cmake_configure_test.py` | 16 | Emscripten 启动方式、静态/安装限制、非 Linux、可选提权工具、插件路径、spdlog/zstd、生成文件、Relay、DEB 依赖 |
| `EncosPluginRegistrationPolicies` | `tests/cmake/test_encos_plugins.py` | 17 | 插件描述合法性、重复/黑名单、自动发现、父项目扩展、静态/动态库类型、安装导出和外部插件消费方 smoke test |

原先这两个脚本通过 `unittest` 自行创建临时源码树/构建目录，并多次调用 CMake；现已删除。

#### Python CMake 契约的逐项清单

<!-- PYTHON-CMAKE-ITEMS-START -->
| 外层 CTest | `unittest` 类 | 具体测试项 | 测什么 |
| --- | --- | --- | --- |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_can_configure_without_spdlog` | 验证关闭 spdlog 且禁止发现该包时，CAN 组合仍可配置。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_deb_depends_on_zstd_with_and_without_spdlog` | 分别启用/关闭 spdlog，验证生成 DEB 的依赖均包含 zstd。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_emscripten_configures_without_zstd` | 验证 Emscripten 构建不依赖 zstd；无 Emscripten SDK 时跳过。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_emscripten_toolchain_requires_env_script_and_emcmake` | 验证 Emscripten 配置须通过 emsdk 环境脚本和 `emcmake`，并拒绝不满足条件的直接配置。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_linux_rejects_unavailable_explicit_plugin` | 验证 Linux 上显式请求不可用的 Windows EtherCAT 插件会失败。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_log_writer_rejects_mismatched_row_width_at_compile_time` | 编译错误行宽调用，验证 `LogWriter` 在编译期拒绝列数不匹配。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_missing_optional_privilege_tools_compile_as_empty_paths` | 验证找不到 `pkexec`/`setcap` 时以空路径编译，功能可降级。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_native_build_requires_zstd` | 验证原生构建缺少 zstd 时配置必须失败。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_no_python_configure_uses_checked_in_generated_files` | 验证关闭 Python 时可使用仓库已提交的生成文件配置。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_non_linux_tests_configure_without_thread_priority_helper` | 模拟非 Linux 平台，验证测试配置不依赖 Linux 的 ThreadPriorityHelper。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_plugin_install_artifacts_share_multiarch_directory` | 验证安装插件产物使用 `CMAKE_INSTALL_LIBDIR` 的多架构目录。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_relay_option_off_configures_successfully` | 验证 Relay 选项关闭时的配置成功。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_relay_option_on_configures_successfully` | 验证 Relay 选项开启时的配置成功。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_static_mode_configures_without_install` | 验证静态模式在关闭安装规则时可以完成配置。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_static_mode_rejects_install` | 验证静态模式开启安装规则会在配置阶段失败。 |
| `EncosCMakeConfigurePolicies` | `CMakeConfigurePolicyTests` | `test_tests_require_python` | 验证启用 `ENCOS_BUILD_TESTS` 但关闭 Python 会失败。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsContractTests` | `test_automatic_scan_adds_valid_and_skips_unavailable` | 验证自动发现会添加有效插件并跳过不可用插件。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsContractTests` | `test_blacklist_ignored_for_explicit_list` | 验证显式插件列表不受自动发现黑名单影响。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsContractTests` | `test_blacklist_skips_plugin_in_automatic_scan` | 验证黑名单能在自动发现时排除指定插件。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsContractTests` | `test_duplicate_adapter_type_fails` | 验证不同插件声明重复 adapter type 会失败。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsContractTests` | `test_duplicate_name_different_path_fails` | 验证相同插件名指向不同路径会失败。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsContractTests` | `test_duplicate_name_same_path_is_idempotent` | 验证同名同路径的重复注册是幂等的。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsContractTests` | `test_dynamic_target_must_be_shared_or_module` | 验证动态插件 target 必须是 SHARED 或 MODULE，而不能是 INTERFACE。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsContractTests` | `test_explicit_empty_list_builds_without_tests` | 验证静态模式、无测试时的空插件列表仍可构建。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsContractTests` | `test_explicit_empty_list_disables_plugins_and_tests_require_fake` | 验证显式空插件列表会禁用插件，且开启测试时因缺少 Fake 而失败。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsContractTests` | `test_explicit_unavailable_plugin_fails_with_reason` | 验证显式注册不可用插件会失败并给出原因。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsContractTests` | `test_explicit_valid_plugin_succeeds_and_builds` | 验证显式注册有效插件可配置并构建。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsContractTests` | `test_malformed_plugin_fails_explicit` | 验证格式错误的插件描述会失败。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsContractTests` | `test_missing_target_plugin_fails` | 验证插件未定义预期 target 时会失败。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsIntegrationTests` | `test_dynamic_base_is_shared` | 验证动态模式的基础库产物为共享库。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsIntegrationTests` | `test_install_export_consumer_smoke` | 暂存安装本库和外部插件，验证 CMake package、公开头文件、库/插件产物及多个下游消费者可配置、构建、运行；仅 POSIX。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsIntegrationTests` | `test_parent_project_calls_add_emd_plugin_after_subdirectory` | 验证父项目在 `add_subdirectory` 后仍可通过公开 API 注册外部插件并构建。 |
| `EncosPluginRegistrationPolicies` | `EncosPluginsIntegrationTests` | `test_static_base_is_static` | 验证静态模式的基础库产物为静态库。 |
<!-- PYTHON-CMAKE-ITEMS-END -->

`test_emscripten_configures_without_zstd` 会在没有 Emscripten SDK 时标记为 skip；`test_install_export_consumer_smoke` 仅在 POSIX 执行。

### 2.3 已移除的 CMake 脚本策略测试

| CTest 名称 | 脚本 | 断言 |
| --- | --- | --- |
| `EncosExternalDevicesHaveNoPollingWorkers` | `tests/no_external_device_polling.cmake` | Battery/IMU/PMS 实现中不得出现轮询线程、定时器或旧消息排空符号 |
| `EncosThreadPriorityPolicies` | `tests/thread_priority_policy.cmake` | 各适配器主循环必须设置优先级 50；生产代码不得保留 CPU 亲和性；内存锁定仅允许线程优先级实现中使用 |
| `EncosSlcanShutdownPolicies` | `tests/slcan_shutdown_policy.cmake` | SLCAN 停止、join、关闭串口、释放串口的调用存在且顺序正确 |

### 2.4 保留的条件 CTest

| CTest 名称 | 启用条件 | 内容 |
| --- | --- | --- |
| `EncosPortThreadSanitizer` | `ENCOS_ENABLE_TSAN=ON` | 用 TSan 构建后，筛选端口、路由、Fake、等待器和删除并发相关 GoogleTest |
| `EncosPortEmscriptenCompile` | CMake 找到 `em++` | 用 Emscripten 单独编译 `tests/core/port_emscripten_compile.cc`，验证 `Port` 头文件可用 |
| `EncosWasmLoggingUnsupported` | `ENCOS_BUILD_WASM_TESTS=ON` | WASM C++ 可执行文件验证不支持的日志行为；由 WASM CTest 执行 |

## 3. WASM 与 TypeScript 测试

WASM 不走 `ENCOS_BUILD_TESTS`，入口为 [`scripts/test-wasm.sh`](../scripts/test-wasm.sh)。它依次配置/构建 `build-wasm`、执行 WASM CTest、TypeScript 类型检查、打包，再运行 Vitest 和 Playwright。

| 层级 | 命令 | 测试项 | 当前数量 |
| --- | --- | --- | ---: |
| WASM C++ | `ctest --test-dir build-wasm` | `EncosWasmLoggingUnsupported` | 1 |
| TypeScript 类型 | `pnpm --dir npm typecheck` | `tsc --noEmit` | 不适用 |
| Node 单元测试 | `pnpm --dir npm test:unit` | `tests/wasm-node/**/*.test.ts` | 31 |
| 浏览器端到端 | `pnpm --dir npm test:browser` | `npm/tests/browser/runtime.spec.ts` | 1 |

Node 单元测试覆盖 C ABI、运行时和资源释放、Fake 控制、电池/IMU、RelayWs、发布包布局；浏览器测试验证 Asyncify 不阻塞事件循环以及 adapter 销毁后子对象失效。Vitest 的单项超时为 10 秒，Playwright 的单项超时为 15 秒，并启动本地 Vite 服务。

### 3.1 WASM/JavaScript 逐项清单

<!-- WASM-JS-ITEMS-START -->
| 框架 | 源文件 | 测试项 | 测什么 |
| --- | --- | --- | --- |
| Playwright | `npm/tests/browser/runtime.spec.ts` | browser keeps Asyncify responsive and Adapter disposal invalidates children | 浏览器 Asyncify 响应性和处置后的子对象失效。 |
| Vitest | `tests/wasm-node/abi-contract.test.ts` | C ABI export snapshot preserves the Adapter-only disposal boundary | C ABI 的 adapter 唯一处置边界。 |
| Vitest | `tests/wasm-node/abi-contract.test.ts` | control ABI keeps Promise wrappers and five-value/five-meta result layout | 控制 ABI 的 Promise 与五值/五元数据布局。 |
| Vitest | `tests/wasm-node/battery.test.ts` | Battery wrapper decodes injected BMS frames into status snapshot | BMS 注入帧到 Battery 状态快照的解码。 |
| Vitest | `tests/wasm-node/battery.test.ts` | Battery wrapper sends passive commands as raw 0x4F4 frame | 被动电池命令的 `0x4F4` 原始帧编码。 |
| Vitest | `tests/wasm-node/fake-adapter.test.ts` | PVT control decodes command with seeded motor ranges | 预置电机范围下 PVT 命令解码。 |
| Vitest | `tests/wasm-node/fake-adapter.test.ts` | auto-create creates snapshot and replies unless reply mode is manual | 自动创建时快照/自动回复，手动模式例外。 |
| Vitest | `tests/wasm-node/fake-adapter.test.ts` | auto-create is disabled by default but commands are still recorded | 默认关闭自动创建但仍记录命令。 |
| Vitest | `tests/wasm-node/fake-adapter.test.ts` | calibration APIs expose current control and position setters | 校准 API 暴露电流控制和位置设置。 |
| Vitest | `tests/wasm-node/fake-adapter.test.ts` | injected feedback updates adapter status cache | 反馈注入更新 adapter 状态缓存。 |
| Vitest | `tests/wasm-node/fake-adapter.test.ts` | listBusKeys includes created and seeded Fake buses | bus key 列表包含创建及预置的 Fake 总线。 |
| Vitest | `tests/wasm-node/fake-adapter.test.ts` | manual speed control records command and returns no response | 手动回包时记录命令并返回无响应。 |
| Vitest | `tests/wasm-node/fake-adapter.test.ts` | parameter write policy can ignore CAN timeout ack | 参数策略忽略 CAN timeout ACK。 |
| Vitest | `tests/wasm-node/fake-adapter.test.ts` | position control supports feedback type 2 and 3 | 位置控制反馈类型 2/3。 |
| Vitest | `tests/wasm-node/fake-adapter.test.ts` | seeded Fake motor records and decodes speed control commands | 预置 Fake 电机的速度命令记录与解码。 |
| Vitest | `tests/wasm-node/fake-adapter.test.ts` | speed control supports feedback type 2 and 3 | 速度控制反馈类型 2/3。 |
| Vitest | `tests/wasm-node/imu.test.ts` | IMU wrapper decodes injected YIS130 frames into status snapshot | YIS130 注入帧到 IMU 状态快照的解码。 |
| Vitest | `tests/wasm-node/imu.test.ts` | IMU wrapper exposes absent optional groups instead of zero-filled values | 缺失 IMU 可选组不填零。 |
| Vitest | `tests/wasm-node/package-layout.test.ts` | package metadata keeps the @encos scope | package 元数据的 `@encos` scope。 |
| Vitest | `tests/wasm-node/package-layout.test.ts` | published package entry exposes the shared public API from Node | Node 中已发布包入口的公开 API。 |
| Vitest | `tests/wasm-node/package-layout.test.ts` | published package entry loads runtime from bundled glue by default | 包入口默认加载打包 WASM glue。 |
| Vitest | `tests/wasm-node/relay-ws-adapter.test.ts` | RelayWs adapter is available in WASM and is not a Fake adapter | WASM RelayWs 可用且不被识别为 Fake。 |
| Vitest | `tests/wasm-node/relay-ws-adapter.test.ts` | RelayWs response completes while Asyncify yields the event loop | Asyncify 让出事件循环时 RelayWs 响应仍完成。 |
| Vitest | `tests/wasm-node/relay-ws-adapter.test.ts` | RelayWs start response must contain a session id | RelayWs 启动响应必须含 session id。 |
| Vitest | `tests/wasm-node/runtime.test.ts` | async motor APIs reject after adapter disposal | adapter 处置后的异步电机 API 拒绝。 |
| Vitest | `tests/wasm-node/runtime.test.ts` | dedicated Fake adapters are isolated and expose fake tools | 专用 Fake adapter 隔离与工具暴露。 |
| Vitest | `tests/wasm-node/runtime.test.ts` | disposing one shared Fake adapter handle preserves the other handle lease | 释放一共享句柄不破坏另一租约。 |
| Vitest | `tests/wasm-node/runtime.test.ts` | failed Adapter disposal restores the wrapper so deletion can be retried | 处置失败恢复 wrapper 以可重试。 |
| Vitest | `tests/wasm-node/runtime.test.ts` | generic Fake adapter uses normal adapter surface only | 通用 Fake 仅提供常规 adapter 接口。 |
| Vitest | `tests/wasm-node/runtime.test.ts` | slave bus keys expose raw index and decoded components | 从站 bus key 的原始和解码索引。 |
| Vitest | `tests/wasm-node/runtime.test.ts` | synchronous adapter disposal cancels an Asyncify waiter before deferred deletion | 同步处置在延迟删除前取消 Asyncify 等待。 |
| Vitest | `tests/wasm-node/runtime.test.ts` | wrapper caches bus and motor handles until adapter disposal | adapter 处置前的 bus/motor 句柄缓存。 |
<!-- WASM-JS-ITEMS-END -->

该脚本需要已安装 Emscripten SDK（默认从 `$HOME/emsdk/emsdk_env.sh` 加载）、`pnpm` 和 npm 依赖；CI 在调用脚本前使用 `pnpm --dir npm install --frozen-lockfile`。

## 4. CI 中的其他验证

`.gitea/workflows/build-native.yml` 还承担下列非单元测试验证。它覆盖 Ubuntu 22.04/24.04、amd64/arm64、动态/静态和含/不含 IGH EtherCAT 的矩阵；并非每个矩阵都会执行每种检查。

| 类别 | CI 检查 | 目的 |
| --- | --- | --- |
| 代码格式 | `cmake --build build --target format-check` | 对 `include`、`src`、`plugins`、`tests` 的 C++ 文件执行 clang-format dry-run |
| 静态分析 | `cmake --build build --target tidy` | 基于 `compile_commands.json` 对项目 `.cc` 翻译单元执行 clang-tidy |
| 负向配置 | 三条预期失败的 `cmake` 命令 | 静态模式安装限制、Windows EtherCAT 平台限制、缺失 IGH 依赖限制 |
| 静态产物 | 查找 `build/plugins` 下的 `.so/.dll` | 静态模式不得生成动态插件 |
| 安装/卸载 | `cmake --install`、检查安装文件、`uninstall` 目标 | 头文件、库、插件目录、IGH 插件安装及卸载流程 |
| 权限 | `getcap` 检查 `*Executable` | fd broker 可执行文件须具有 `cap_net_raw` |
| 打包 | `cpack` | 发布条件满足时构建 DEB 包 |

`.gitea/workflows/build-wasm.yml` 负责安装 npm 依赖后调用 `scripts/test-wasm.sh`。它还在版本变更时执行 npm 打包检查并发布；发布步骤不是测试。

## 5. 真机 benchmark 与第三方测试

### 5.1 真机 benchmark

`ENCOS_ENABLE_BENCH`、`EncosManagerHotPathBenchmark` 与 `tests/manager_hot_path_benchmark.cc` 已删除。时延 benchmark 必须在真实电机、总线和负载上执行，不属于本仓库的自动化 pass/fail 流程。

### 5.2 第三方依赖自带测试（默认不纳入主项目）

| 依赖目录 | 自带测试 | 为什么不在主 CTest 基线 |
| --- | --- | --- |
| `external/IXWebSocket/test` | IXWebSocket 的 CTest/GoogleTest | 根项目把 `USE_TEST` 强制为 `OFF` |
| `external/dylib/tests` | `unit_tests` GoogleTest | `DYLIB_BUILD_TESTS` 默认 `OFF` |
| `external/boost_preprocessor/test` | Boost.Test 编译、编译失败和运行测试 | 根项目仅使用头文件，未把该依赖作为子目录加入 |
| `external/SOEM/samples/*` | EtherCAT 示例（含 `eni_test`、`eoe_test`） | 示例程序而非 Encos 自动化测试；部分需要现场 EtherCAT 环境 |

第三方测试保持独立层级，除非明确决定让 CI 覆盖供应商依赖；不要把它们混入 Encos 自身的质量指标。

## 6. 当前分层边界

1. **快速、确定性的核心单元测试**：两套 GoogleTest，允许按标签/目标/领域筛选。
2. **构建与发布验证**：生产 CMake 的选项约束、安装、打包与 CI smoke 检查，不作为 CTest 元测试。
3. **平台与工具链验证**：TSan、Emscripten 编译、WASM C++、Node、浏览器测试，必须显式声明依赖和运行环境。
4. **性能和真实硬件**：独立的真机 benchmark/stress 工作流，不进入本仓库常规 CI 的 pass/fail 门禁。

## 7. 盘点与复核命令

```bash
# 查看当前构建实际注册的 CTest 项（不运行测试）
ctest --test-dir build -N

# 查看可按正则筛选的 CTest 名称
ctest --test-dir build -N -R '^(EncosManagerShutdown|EncosPort|.*Tests\\.)'

# 运行原生全部测试
ctest --test-dir build --output-on-failure

# 运行 WASM 全链路（需 emsdk、pnpm 依赖）
bash scripts/test-wasm.sh
```

测试注册源的权威位置是 `cmake/EncosTests.cmake`、各插件的 `CMakeLists.txt`、根 `CMakeLists.txt` 及 `scripts/test-wasm.sh`；构建目录中的 `CTestTestfile.cmake` 和 `*Tests[1]_tests.cmake` 是特定配置生成的快照。
