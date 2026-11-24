// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

export {
  FakeCommandKind,
  FakeReplyMode,
  FakeWritePolicy,
  LogLevel,
  MotorModel,
  MotorParameter,
} from './constants.js'
export { createEncosRuntime } from './create-runtime-native.js'
export { checkPermissions, requestPermissions } from './permissions-native.js'
export type { PermissionStatus } from './permissions-native.js'
export type {
  BusKey,
  BatteryPassiveCommands,
  BatteryStatusResult,
  ControlResult,
  CreateAdapterOptions,
  CreateFakeAdapterOptions,
  CurrentControlCommand,
  FakeCommandRecord,
  FakeFeedbackInput,
  FakeRawMessageRecord,
  ImuStatusResult,
  PosControlCommand,
  PVTControlCommand,
  SpdControlCommand,
  StatusResult,
  WebSerialBridge,
  WebSerialOpenOptions,
  WebSerialPort,
  WebSerialRequestOptions,
  WebSerialStatus,
} from './types.js'

export type { NativeAddon, NativeRuntimeOptions as RuntimeOptions } from './create-runtime-native.js'

export * from './native-types.js'
export { MotorStopMode, MotorCommunicationMode, MotorError, CanFrameFlag, PmsCommand,
  GloveCalibrationStatus, GloveFinger, kDecodedFloatTolerance, kDecodedAngleTolerance,
  sanitizeCanFrameFlags, canFrameFlagsUseExtendedId, canFrameFlagsUseRtr,
  canFrameFlagsUseCanFd, canFrameFlagsHaveValidCanFdBits, gloveCalibrationEncoderMask,
} from './constants.js'
export { Runtime } from './runtime.js'
export { AdapterWrapper, FakeAdapterWrapper } from './adapter.js'
export { BusWrapper } from './bus.js'
export { MotorWrapper } from './motor.js'
export { BatteryWrapper } from './battery.js'
export { ImuWrapper } from './imu.js'
export { PmsWrapper } from './pms.js'
export { DexHandWrapper } from './dex-hand.js'
export { GloveWrapper } from './glove.js'
export { FakeTools } from './fake-tools.js'

export { DriverManager } from './driver-manager.js'
export type { ControlCommand } from './types.js'

export { Logger, LogWriter } from './logging.js'
export type { LogArgument, LogField } from './logging.js'

export { logLevelFromInt, logLevelToInt, logLevelToString, logLevelFromString } from './constants.js'

export type {
  AdapterStatistics,
  BusStatistics,
  CanBusStatistics,
  CanErrorCounters
} from './adapter.js'

export { StatisticsStatus, ErrorCounterKind } from './adapter.js'
