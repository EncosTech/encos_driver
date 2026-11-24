# Native JavaScript API coverage

`@encos/encos-driver-native` selects the installed platform backend and forwards every runtime export unchanged. Its bundled declarations expose the same complete API without requiring any platform-specific package during type checking.

The native entry exports the existing WASM-shaped API plus the application-facing built-in C++ API below. Existing controls remain Promise-returning. New operations that wait for hardware run on native workers; cached-state reads and local configuration are synchronous. Extended native operations called on the WASM runtime throw an explicit unsupported-operation error.

All names below are methods unless marked as exported constants/types. `runtime` is returned by `createEncosRuntime()`. Native calls use structured Node-API values, preserving NaN, optional fields and 64-bit IDs (BigInt). No WASM memory is simulated.

## Runtime, adapter and bus

| C++ surface | JavaScript surface / native operation |
| --- | --- |
| `encos_get_version` | `runtime.getVersion()` / `runtime.getVersion` |
| `MakeAdapter`, manager `CreateAdapter` (both overloads) | `runtime.createAdapter({type, interfaceName, loggerName?, logLevel?, extend?})` / `runtime.createAdapter` |
| `SetPluginPath` | `runtime.setPluginPath(path)` / `runtime.setPluginPath` |
| `GetAvailableAdapterTypes`, `GetAvailableInterface` | `runtime.getAvailableAdapterTypes()`, `runtime.getAvailableInterface(type)` |
| `ClearLogger` | `runtime.clearLogger()` |
| `BaseAdapter::GetBus` (both overloads) | `adapter.getBus(index)` / `adapter.getBus(slaveIndex, busIndex)` |
| `GetBuses`, `GetGlove`, `Ok`, `GetInterfaceName` | `adapter.getBuses(): Map`, `getGlove(slaveIndex)`, `ok()`, `getInterfaceName()` |
| `GetFakeAdapterControl` | `adapter.getFakeAdapterControl()` (undefined when unsupported) |
| `UpdateBusExtend` | `adapter.updateBusExtend(extend)` |
| `Commit`, `SetSyncMode` | `adapter.commit()`, `adapter.setSyncMode(enabled)` |
| `GetMotorStatus()` | `adapter.getMotorStatus(): Map<bigint, MotorStatus>` |
| `GetMotorStatus(bus, motor, deduction)` | `adapter.getMotorStatus(busIndex, motorIndex, lifeCycleDeduction = 1)` |
| `SetMaxStatusLifeCycle`, `GetMaxStatusLifeCycle` | `adapter.setMaxStatusLifeCycle(value)`, `getMaxStatusLifeCycle()` |
| `SetStatusMedianFilterWindowSize`, `SetStatusLimitFilterMaxDeltas` | `adapter.setStatusMedianFilterWindowSize(size)`, `setStatusLimitFilterMaxDeltas(deltas)` |
| `SetOnStatus(bus, motor, callback)` | `adapter.setOnStatus(busIndex, motorIndex, callbackOrNull)` / subscription `adapter.onStatus` |
| `Bus::GetMotor` (six overloads), manager `CreateMotor` frame flags | `await bus.getMotor(index, modelOrOptionsOrPropertiesOrCanFd?, canfd?)`; options `{model?, properties?, canfd?, frameFlags?}` |
| `GetBattery`, `GetImu`, `GetPms` | `bus.getBattery(index)`, `getImu(index)`, `getPms()` |
| `GetDexHand` | `bus.getDexHand(baseId = 1)` |
| `SelectMotor`, `GetMotors` | `bus.selectMotor(index)` (undefined when absent), `bus.getMotors(): Map` |
| `ScanMotors`, `DetectExternalDevice`, `ResetMotorsId` | `await bus.scanMotors()`, `detectExternalDevice()`, `resetMotorsId(waitForAck = true)` |
| `HasExternalDevice`, `StartImu`, `StopImu` | `bus.hasExternalDevice()`, `startImu()`, `stopImu()` |
| `StartAutoAssignCanId`, `StopAutoAssignCanId` | `bus.startAutoAssignCanId(startIndex)`, `stopAutoAssignCanId()` |
| `Commit`, `SetSyncMode`, `GetBusIndex` | `bus.commit()`, `setSyncMode(enabled)`, `getBusIndex()` / `index` |
| Built-in `Delete*` / `Destroy*` | Corresponding wrapper `.dispose()`; runtime `.dispose()` releases its entire scope |

Wrapper caches reuse live handles. Native motor `index` is queried from the manager so ID migration is visible. Disposal invalidates native handles and descendants; repeated local disposal is idempotent.

## Motor

| C++ surface | JavaScript surface |
| --- | --- |
| `EnableLog`, `DisableLog`, `IsLogged` | `enableLog(baseName)`, `disableLog()`, `isLogged()` |
| `GetProperties`, `SetProperties`, `HasBrake` | `getProperties()`, `setProperties(properties)`, `hasBrake()` |
| `InitMotorPVTParam` | `await initMotorPvtParam()` |
| Deprecated `GetPVTRanges`, `SetDriverPVTRanges`, `SetCurrentRange` | `getPvtRanges()`, `setDriverPvtRanges(properties)`, `setCurrentRange(range)` |
| `EnableCanFd`, `DisableCanFd`, `IsCanFdEnabled` | `enableCanFd()`, `disableCanFd()`, `isCanFdEnabled()` |
| `EnableCanEff`, `DisableCanEff`, `IsCanEffEnabled` | `enableCanEff()`, `disableCanEff()`, `isCanEffEnabled()` |
| `SetId`, `SetPos`, `ResetZeroPos` | `await setId(newIndex, waitForAck = true)`, `setPosition(position)`, `resetZeroPosition(waitForAck = true)` |
| `GetStatus` | `getStatus(lifeCycleDeduction = 1): StatusResult` |
| `SetOnStatus` | `setOnStatus(callbackOrNull)` / `motor.onStatus`; callback receives `MotorStatus` |
| `SetMaxStatusLifeCycle`, `SetStatusMedianFilterWindowSize` | `setMaxStatusLifeCycle(value)`, `setStatusMedianFilterWindowSize(size)` |
| Both `SetStatusLimitFilterMaxDeltas` overloads | `setStatusLimitFilterMaxDeltas({position, speed, current, motorTemperature, mosTemperature})` |
| `PVTControl<0/1>` | `await pvtControl({kp, kd, position, speed, torque, feedbackType?})` |
| `PosControl`, `SpdControl`, `CurControl` | Existing `posControl`, `spdControl`, `curControl` command objects; optional independent `feedback` added |
| `TorControl`, `Stop` | `await torControl({torque, feedbackType?, feedback?})`, `stop({mode, current, feedbackType?, feedback?})` |
| `Brake` | `await brake(enabled, waitForAck = true)` |
| Six `SetPVT*Range` setters | `await setPvtKpRange`, `setPvtKdRange`, `setPvtPosRange`, `setPvtSpdRange`, `setPvtTorRange`, `setPvtCurRange` with `({min,max}, waitForAck = true)` |
| `SetCurPI`, `SetSpdPI`, `SetPosPD` | `await setCurPi({kp,ki}, waitForAck = true)`, `setSpdPi(...)`, `setPosPd({kp,kd}, waitForAck = true)` |
| `SetCanTimeout`, `SetCommunicationMode` | `await setCanTimeout(timeoutMs, waitForAck = true)`, `setCommunicationMode(mode, waitForAck = true)` |
| `GetParameter<Param>` | `await getParameter(parameter)` with a parameter-indexed TypeScript result; existing `readPosition`, `readSpeed`, `readCurrent` remain |

`feedbackType` selects the C++ template/waiting behavior; `feedback` selects the wire-requested reply, defaulting to `feedbackType`. Existing `ControlResult` stays normalized; unsupported fields in feedback types 2/3 remain NaN. PVT only accepts feedback types 0/1 and always requests feedback on the wire. All control calls remain Promise-returning, including type 0.

The complete `MotorParameter` enum and typed results are exported: Position=1, Speed=2, Current=3, Power=4, Kt=22; PVT ranges=23–28; UUID=29; CanTimeout=31; CurKpKi=32, SpdKpKi=33, PosKpKd=34; BrakeStatus=37; EncoderAngle=39; ProductId=48, HardwareVersion=49, FirmwareVersion=50. The gaps are preserved. Unsigned version/product values remain unsigned numbers.

## Battery, IMU, PMS and Glove

| C++ surface | JavaScript surface |
| --- | --- |
| Battery `GetStatus`, `SendPassiveCommands`, `ClearFault`, `RequestCharging`, `AllowDischarge` | Existing `getStatus`, `sendPassiveCommands`, `clearFault`, `requestCharging`, `allowDischarge` |
| Battery `SetOnStatus`, `BatteryError::AnyError` | `battery.setOnStatus(callbackOrNull)`, exported `batteryHasAnyError(error)` |
| IMU `GetStatus`, `SetOnStatus` | `imu.getStatus()`, `imu.setOnStatus(callbackOrNull)` |
| IMU `NativeUpdateInterval`, `JsUpdateInterval` | `ImuWrapper.nativeUpdateInterval()` = 5 ms, `ImuWrapper.jsUpdateInterval()` = 20 ms |
| PMS `GetStatus`, `SetOnStatus`, `SendCommand` | `pms.getStatus(): PmsStatus \| undefined`, `setOnStatus(callbackOrNull)`, `sendCommand(mask)` |
| Glove `GetStatus`, `SetOnStatus` | `glove.getStatus()` (5×10 optional angles), `setOnStatus(callbackOrNull)` |
| Glove `CalibrateAll`, `CalibrateByMask` | `await glove.calibrateAll()`, `await glove.calibrateByMask(fingerIndex, encoderMask)` |
| `ENCOS_GLOVE_CALI_E` | `gloveCalibrationEncoderMask(index)` |

Named data types cover every C++ field: MotorProperties, Range, KpKi, KpKd, MotorEncoderAngle, all feedback forms, BatteryState/Temp/Error/ActiveCommands/PassiveCommands/Status, ImuAcceleration/AngularVelocity/EulerAngle/Quaternion/Status, PmsStatus and GloveStatus. Existing `*StatusResult` types are retained. Missing optional groups/angles are undefined. PMS expiry and Glove whole-hand callback aggregation follow the core implementation.

## DexHand

`bus.getDexHand(baseId)` returns a `DexHandWrapper` for the gateway at base ID `1`–`247`. Use `0` for broadcast discovery with one gateway on the bus. The wrapper offers four asynchronous methods.

| Method | Reply value on success |
| --- | --- |
| `getBaseId(options?)` | Current base ID |
| `setBaseId(baseId, options?)` | New base ID |
| `getSamplePoints(options?)` | `{ nominal, data }` |
| `setSamplePoints({ nominal, data }, options?)` | Saved `{ nominal, data }` |

Each resolves to `{ status, value? }`. `DexHandStatus` provides the status codes. Requests default to classic CAN and a 500 ms reply timeout. Options are `{ canfd?, timeoutMs? }`. Both sample points use units of 0.1%, and saved settings take effect after the gateway restarts. After a successful base-ID change, dispose the old wrapper with `runtime.deleteDexHand(hand)` and get a wrapper for the new address. Disposing the bus or adapter also releases the wrapper.

## Fake controls

| C++ surface | JavaScript surface |
| --- | --- |
| `EnableAutoCreateMotor`, `DisableAutoCreateMotor`, `SetReplyMode` | `fake.enableAutoCreateMotor()`, `disableAutoCreateMotor()`, `setReplyMode(mode)` |
| `EnableCommandRecording`, `EnablePositionError` | `fake.enableCommandRecording(enabled = true)`, `enablePositionError(enabled = true)` |
| `SetDecodedCommandObserver`, `ClearDecodedCommandObserver` | `setDecodedCommandObserver(callbackOrNull)`, `clearDecodedCommandObserver()` / subscription `fake.decodedCommand` |
| `CreateSnapshot`, `GetMotorSnapshot` | `createSnapshot(model)`, `getMotorSnapshot(busIndex, motorIndex)` |
| Both `SeedMotor` overloads | `seedMotor(busIndex, motorIndex, model, {replyFrameFlags?})` or `seedMotor(busIndex, motorIndex, snapshot)` |
| `SetParameterWritePolicy` | Existing `setParameterWritePolicy` |
| Full `GetCommandRecords` | `getDecodedCommandRecords()`, `getLastDecodedCommand()`; existing numeric-payload `getCommandRecords()` / `getLastCommand()` preserved |
| `GetFormattedSentCommands`, `GetReplyRecords`, `GetRawSentMessages`, `ClearCommandRecords` | `getFormattedSentCommands()`, `getReplyRecords()`, `getRawSentMessages()`, `clearCommandRecords()` |
| `InjectMessage` | `injectMessage(message)`; existing `injectRawMessage` preserved |
| `MakeFeedbackMessage`, `MakeParameterReply`, `MakeWriteAck` | `makeFeedbackMessage(busIndex,motorIndex,status,feedbackType=1)`, `makeParameterReply(busIndex,motorIndex,parameter,payload,frameFlags=0)`, `makeWriteAck(...)` |
| Feedback construction + injection | Existing `injectFeedback` preserved |

Full native snapshots include ranges/brake/firmware version, optional firmware probe response, product/hardware versions, raw loop gains, calibration flags and encoder angles. Full decoded records use a discriminated payload union and include raw frame flags and formatted text. The legacy record API remains compatible.

## Constants and lifecycle

All 72 MotorModel values are generated from the C++ CSV by `npm/scripts/generate-models.mjs`. All MotorParameter, MotorError, MotorStopMode, MotorCommunicationMode, FakeReplyMode, FakeWritePolicy, FakeCommandKind, PmsCommand and Glove enums are exported with their exact numeric values. Model conversion/range helpers are runtime methods. CAN frame constants/validation helpers, message types/equality and decoded tolerances are exported.

Callbacks are delivered on the owning JavaScript event loop, never directly on native receive threads. Null unregisters the handler. Replacement, parent disposal, runtime disposal and Worker termination invalidate callback registrations and stale queued deliveries. JavaScript callbacks may initiate asynchronous work or disposal without executing on the C++ receive thread.

Custom Device plugins are excluded. Private implementation methods, test hooks, native subclass factories, protected transport writers and C++ ABI/plugin-author machinery are not application operations. Device constructors are represented by parent factory methods; C++ destructors by disposal. Low-level utilities outside this application-facing scope are tracked separately.

## Manager and application utilities

`runtime.manager` exposes the application-facing `EncosDriverManager` operations in the current runtime scope:

- `createAdapter`, `createBus`, `createMotor`, `createBattery`, `createImu`, `createPms`, `createDexHand`, `createGlove` delegate to the same factories and caches.
- `findMotor`, `getMotors`, `getBuses` return existing wrappers.
- `migrateMotorIndex(motor,index)` updates the core routing/index without sending a motor command.
- `destroyAdapter`, `destroyBus`, `destroyMotor`, `destroyBattery`, `destroyImu`, `destroyPms`, `destroyDexHand`, `destroyGlove`, `destroyAdapterByInterfaceName` expose scoped destruction. Runtime also offers `delete*`, `deleteAdapterByInterfaceName`, and the deprecated-compatible `unloadAdapterByInterfaceName` spelling.
- `dispatchReceive(adapter,busIndex,message)` and `dispatchUnknownReceive(...)` accept typed `MotorPackMsg` values.
- `makeReceiveUniqueId(busIndex,canId)` always returns a BigInt.

`runtime.createLogger(name, level = LogLevel.Info)` returns a `Logger` with `setLevel`, `level`, `name`, `flush`, `trace`, `debug`, `info`, `warn`, `error`, `critical`, and `dispose`. Formatting follows the existing C++ Logger: each argument replaces the next `{...}` segment; unused arguments append with spaces. `logLevelFromInt`, `logLevelToInt`, `logLevelFromString`, `logLevelToString` are exported; `runtime.clearLoggers()` aliases `clearLogger()`.

`runtime.createLogWriter(baseName, headers)` replaces the compile-time C++ column count with the header-array length. Its `write(...fields)`, `flush`, `getBaseName`, `getFileName`, and `dispose` methods preserve fixed-width CSV/Zstd behavior. Fields accept strings, numbers (including NaN/infinity), BigInt, booleans and null/undefined optional values. Runtime disposal flushes owned writers. `runtime.setCurrentThreadPriority(priority)` operates synchronously on the calling JavaScript thread, matching the C++ helper.

`setProperties` preserves `kt`, `brake`, and `fwVersion`, as the C++ implementation does. SetId without acknowledgement sends the request without migrating the local manager index. Queued Promise operations do not force a synchronous index read during SetId completion.

## Soft synchronization and response timeouts

Soft sync buffers only the five motor motion-control families (`pvtControl`, `posControl`, `spdControl`, `curControl`, `torControl`) among motor operations. Motor queries/configuration/initialization, `stop`, `brake`, IMU/PMS commands and bus discovery/ID-management requests submit immediately without an implicit commit or clearing pending controls. Battery, Glove and custom-device scheduling policies are unchanged. A control call waiting for feedback still requires a timely commit; the native runtime serializes its async work, so use feedback type 0 followed by commit for the usual JS soft-sync loop.

Normal motor response/ACK waits time out after 200 ms. The firmware-version probe retains its dedicated 100 ms timeout. These host-side waits do not modify the device watchdog set by `setCanTimeout`. EtherCAT commits fill free slots after previously submitted immediate messages, preserve separate commit boundaries per bus and never overwrite an occupied slot solely because the CAN ID matches. Existing queue overload policies still apply.
