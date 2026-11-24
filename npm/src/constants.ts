// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

export const LogLevel = Object.freeze({
  Trace: 0,
  Debug: 1,
  Info: 2,
  Warn: 3,
  Error: 4,
  Critical: 5,
  Off: 6,
})

export { MotorModel } from './motor-models-generated.js'

export const MotorParameter = Object.freeze({
  Position: 1, Speed: 2, Current: 3, Power: 4, Kt: 22,
  PVTKpRange: 23, PVTKdRange: 24, PVTPosRange: 25, PVTSpdRange: 26,
  PVTTorRange: 27, PVTCurRange: 28, UUID: 29, CanTimeout: 31,
  CurKpKi: 32, SpdKpKi: 33, PosKpKd: 34, BrakeStatus: 37,
  EncoderAngle: 39, ProductId: 48, HardwareVersion: 49, FirmwareVersion: 50,
} as const)

export const FakeReplyMode = Object.freeze({
  Automatic: 0,
  Manual: 1,
})

export const FakeWritePolicy = Object.freeze({
  Success: 0,
  Failure: 1,
  Ignore: 2,
})

export const FakeCommandKind = Object.freeze({
  Unknown: 0,
  PVTControl: 1,
  PosControl: 2,
  SpdControl: 3,
  CurControl: 4,
  TorControl: 5,
  Stop: 6,
  Brake: 7,
  SetId: 8,
  SetPos: 9,
  ResetZeroPos: 10,
  GetParameter: 11,
  SetParameter: 12,
})

export const MotorStopMode = Object.freeze({ FullBrake: 2, DynamicBrake: 3, RegenerativeBrake: 4 } as const)
export const MotorCommunicationMode = Object.freeze({ ClassicCan: 0, CanFd: 1, CanOpen: 2 } as const)
export const MotorError = Object.freeze({
  NoError: 0, OverTemperature: 1, OverCurrent: 2, VoltageHigh: 3, VoltageLow: 4,
  EncoderError: 5, BrakeVoltageHigh: 6, DriverError: 7, OverTemperatureWarning: 8,
  OutputEncoderError: 9, FetOverTemperature: 10, OverSpeed: 11, TemperatureWarning: 12,
  NoResponse: 255,
} as const)
export const CanFrameFlag = Object.freeze({ Eff: 1, FdBit1: 2, FdBit2: 4, Rtr: 8, Mask: 15, FdMask: 6 } as const)
export const PmsCommand = Object.freeze({
  None: 0, DisableChannel1: 1, DisableChannel2: 2, DisableChannel3: 4,
  DisableChannel4: 8, DisableChannel5: 16, DisableChannel6: 32,
  EnableChannel1: 256, EnableChannel2: 512, EnableChannel3: 1024,
  EnableChannel4: 2048, EnableChannel5: 4096, EnableChannel6: 8192,
} as const)
export const GloveCalibrationStatus = Object.freeze({ Success: 160, Failed: 161, Limited: 162, Timeout: 0 } as const)
export const GloveFinger = Object.freeze({ Thumb: 0, Index: 1, Middle: 2, Ring: 3, Pinky: 4 } as const)
export const kDecodedFloatTolerance = 0.05
export const kDecodedAngleTolerance = 0.01
export const sanitizeCanFrameFlags = (flags: number): number => flags & CanFrameFlag.Mask
export const canFrameFlagsUseExtendedId = (flags: number): boolean => (flags & CanFrameFlag.Eff) !== 0
export const canFrameFlagsUseRtr = (flags: number): boolean => (flags & CanFrameFlag.Rtr) !== 0
export const canFrameFlagsUseCanFd = (flags: number): boolean => (flags & CanFrameFlag.FdMask) === CanFrameFlag.FdMask
export const canFrameFlagsHaveValidCanFdBits = (flags: number): boolean => (flags & CanFrameFlag.FdMask) === 0 || canFrameFlagsUseCanFd(flags)
export function gloveCalibrationEncoderMask(index: number): number {
  if (!Number.isInteger(index) || index < 0 || index > 9) throw new RangeError('encoder index must be 0..9')
  return 1 << index
}

export function logLevelFromInt(value: number): number {
  if (!Number.isInteger(value)) throw new TypeError('log level must be an integer')
  return value < 0 || value > 6 ? LogLevel.Info : value
}
export function logLevelToInt(value: number): number { return value }
export function logLevelToString(value: number): string {
  return ['trace', 'debug', 'info', 'warn', 'error', 'critical', 'off'][value] ?? 'info'
}
export function logLevelFromString(value: string): number {
  const normalized = value.toLowerCase()
  if (normalized === 'warning') return LogLevel.Warn
  if (normalized === 'err') return LogLevel.Error
  const index = ['trace', 'debug', 'info', 'warn', 'error', 'critical', 'off'].indexOf(normalized)
  return index === -1 ? LogLevel.Info : index
}
