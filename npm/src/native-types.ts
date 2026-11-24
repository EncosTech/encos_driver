// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import type { MotorModel, MotorParameter, MotorError, MotorStopMode, MotorCommunicationMode, GloveFinger, GloveCalibrationStatus, LogLevel, FakeReplyMode, FakeWritePolicy, FakeCommandKind, PmsCommand, CanFrameFlag } from './constants.js'
import type { BatteryStatusResult, ImuStatusResult, StatusResult } from './types.js'
export type MotorModelValue = typeof MotorModel[keyof typeof MotorModel]
export type MotorParameterValue = typeof MotorParameter[keyof typeof MotorParameter]
export type MotorErrorValue = typeof MotorError[keyof typeof MotorError]
export type MotorStopModeValue = typeof MotorStopMode[keyof typeof MotorStopMode]
export type MotorCommunicationModeValue = typeof MotorCommunicationMode[keyof typeof MotorCommunicationMode]
export type GloveFingerValue = typeof GloveFinger[keyof typeof GloveFinger]
export type GloveCalibrationStatusValue = typeof GloveCalibrationStatus[keyof typeof GloveCalibrationStatus]
export type LogLevelValue = typeof LogLevel[keyof typeof LogLevel]
export type FakeReplyModeValue = typeof FakeReplyMode[keyof typeof FakeReplyMode]
export type FakeWritePolicyValue = typeof FakeWritePolicy[keyof typeof FakeWritePolicy]
export type FakeCommandKindValue = typeof FakeCommandKind[keyof typeof FakeCommandKind]
export type PmsCommandValue = typeof PmsCommand[keyof typeof PmsCommand]
export type CanFrameFlagValue = typeof CanFrameFlag[keyof typeof CanFrameFlag]
export type FeedbackType = 0 | 1 | 2 | 3
export interface Range { min: number; max: number }
export interface KpKi { kp: number; ki: number }
export interface KpKd { kp: number; kd: number }
export interface MotorProperties {
  kp: Range; kd: Range; position: Range; speed: Range; torque: Range; current: Range
  kt: number; brake: boolean; fwVersion: number
}
/** @deprecated 使用 MotorProperties。 */
export type MotorPVTRanges = MotorProperties
export interface MotorEncoderAngle { calFlag: number; rawAngle: number; calAngle: number }
export interface MotorParameterValues {
  1: number; 2: number; 3: number; 4: number; 22: number
  23: Range; 24: Range; 25: Range; 26: Range; 27: Range; 28: Range
  29: number; 31: number; 32: KpKi; 33: KpKi; 34: KpKd
  37: number; 39: MotorEncoderAngle; 48: number; 49: number; 50: number
}
export type MotorStatus = NonNullable<StatusResult['status']>
export type MotorFeedbackMsg1 = MotorStatus
export type MotorFeedbackMsg2 = Pick<MotorStatus, 'error' | 'position' | 'current' | 'motorTemperature'>
export type MotorFeedbackMsg3 = Pick<MotorStatus, 'error' | 'speed' | 'current' | 'motorTemperature'>
export type MotorStatusDeltas = Omit<MotorStatus, 'error'>
export interface MotorOptions { model?: number; properties?: MotorProperties; canfd?: boolean; frameFlags?: number }
export interface TorqueControlCommand { torque: number; feedbackType?: number; feedback?: number }
export interface StopCommand { mode: number; current: number; feedbackType?: number; feedback?: number }
export type BatteryStatus = BatteryStatusResult
export type BatteryState = NonNullable<BatteryStatus['state']>
export type BatteryTemp = NonNullable<BatteryStatus['temp']>
export type BatteryError = BatteryStatus['error']
export type BatteryActiveCommands = NonNullable<BatteryStatus['activeCommands']>
export type ImuStatus = ImuStatusResult
export type ImuAcceleration = NonNullable<ImuStatus['acceleration']>
export type ImuAngularVelocity = NonNullable<ImuStatus['angularVelocity']>
export type ImuEulerAngle = NonNullable<ImuStatus['eulerAngle']>
export type ImuQuaternion = NonNullable<ImuStatus['quaternion']>
export interface PmsStatus {
  v48ChannelEnabled: [boolean, boolean, boolean, boolean, boolean, boolean]
  batterySoc: number; batteryVoltage: number; batteryCurrent: number; v5Current: number
  v48Currents: [number, number, number, number, number, number]
  v19Currents: [number, number]
}
/** 灵巧手管理请求状态。 */
export const DexHandStatus = Object.freeze({
  Success: 0, InvalidParameter: 1, SaveFailed: 2, FirmwareBusy: 3,
  Timeout: 4, Canceled: 5, RequestInProgress: 6,
} as const)
export type DexHandStatus = typeof DexHandStatus[keyof typeof DexHandStatus]
/** CAN 仲裁域与数据域的采样点，单位为 0.1%。 */
export interface DexHandSamplePoints { nominal: number; data: number }
export interface DexHandRequestOptions {
  /** 使用标准 CAN FD 数据帧，默认使用经典 CAN。 */
  canfd?: boolean
  /** 报文提交后等待应答的毫秒数，默认为 500。 */
  timeoutMs?: number
}
export interface DexHandReply<T> { status: DexHandStatus; value?: T }
export type GloveEncoderStatus = number | undefined
export type GloveFingerStatus = [GloveEncoderStatus, GloveEncoderStatus, GloveEncoderStatus, GloveEncoderStatus, GloveEncoderStatus, GloveEncoderStatus, GloveEncoderStatus, GloveEncoderStatus, GloveEncoderStatus, GloveEncoderStatus]
export type GloveStatus = [GloveFingerStatus, GloveFingerStatus, GloveFingerStatus, GloveFingerStatus, GloveFingerStatus]
export type StatusCallback<T> = ((status: T) => void) | null
export interface MotorPackMsg { id: number; frameFlags: number; len: number; data: number[] }
export interface MotorMessage { busIndex: number; data: MotorPackMsg }
export type MotorMessages = MotorMessage[]
export interface FakeSeedOptions { replyFrameFlags?: number }
export interface FakeMotorSnapshot {
  model: number; ranges: MotorProperties
  positionRad: number; speedRadS: number; currentA: number; torqueNm: number
  motorTempC: number; mosTempC: number; acceleration: number; kt: number
  canTimeoutMs: number; replyFrameFlags: number; communicationMode: number; error: number; brakeEnabled: boolean
  fwVersion?: number; productId: number; hwVersion: number
  curGainRaw: KpKi; spdGainRaw: KpKi; posGainRaw: KpKd
  calFlag: number; rawAngleDeg: number; calAngleDeg: number
}
export interface FakeReplyRecord { busIndex: number; motorIndex: number; commandKind: number; policy: number; automatic: boolean }
export interface FakePayloads {
  0: Record<string, never>
  1: { kp: number; kd: number; position: number; speed: number; torque: number }
  2: { position: number; speed: number; current: number; feedbackType: number }
  3: { speed: number; current: number; feedbackType: number }
  4: { current: number; feedbackType: number }
  5: { torque: number; feedbackType: number }
  6: { mode: number; current: number; feedbackType: number }
  7: { enabled: boolean; waitForAck: boolean }
  8: { sourceId: number; targetId: number; waitForAck: boolean }
  9: { positionRad: number; waitForAck: boolean }
  10: { waitForAck: boolean; isLegacyReset: boolean }
  11: { parameter: number }
  12: { parameter?: number; rawParameterId: number; rawValue: number[]; waitForAck: boolean }
}
export type DecodedFakeCommandRecord = {
  [K in keyof FakePayloads]: { kind: K; busIndex: number; motorIndex: number; rawFrameFlags: number; formattedText: string; payload: FakePayloads[K] }
}[keyof FakePayloads]
export function batteryHasAnyError(error: BatteryError): boolean { return Object.values(error).some(Boolean) }
export function motorPackMessagesEqual(a: MotorPackMsg, b: MotorPackMsg): boolean {
  return a.id === b.id && a.frameFlags === b.frameFlags && a.len === b.len && Array.from({ length: 64 }, (_, i) => i).every(i => (a.data[i] ?? 0) === (b.data[i] ?? 0))
}
