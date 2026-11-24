// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import { NumericBuffer } from './backend.js'
import { allocU32, assertOk, getI32 } from './native.js'
import type {
  CFunction,
  ControlResult,
  CurrentControlCommand,
  PosControlCommand,
  PVTControlCommand,
  SpdControlCommand,
  StatusResult,
} from './types.js'
import type { BusWrapper } from './bus.js'
import type { MotorProperties, MotorStatus, MotorStatusDeltas, MotorParameterValue, MotorParameterValues, Range, KpKi, KpKd, TorqueControlCommand, StopCommand, StatusCallback } from './native-types.js'
import { MotorParameter } from './constants.js'

export class MotorWrapper {
  bus: BusWrapper
  runtime: BusWrapper['runtime']
  handle: number
  #index: number
  get index(): number {
    if (this.runtime.backend.kind === 'native') return this.#invoke('getIndex')
    return this.#index
  }
  disposed: boolean

  constructor(bus: BusWrapper, handle: number, index: number) {
    this.bus = bus
    this.runtime = bus.runtime
    this.handle = handle
    this.#index = index
    this.disposed = false
  }

  getStatus(lifeCycleDeduction = 1): StatusResult {
    this.#assertLive()
    if (this.runtime.backend.kind === 'native') return this.#invoke('getStatus', { lifeCycleDeduction })
    const values = new NumericBuffer('double', 5)
    const meta = new NumericBuffer('i32', 4)
    try {
      const code = this.runtime.calls.motorGetStatus(this.handle, lifeCycleDeduction, values, 5, meta, 4)
      assertOk(this.runtime, code, 'failed to get motor status')
      return {
        ok: meta.get(0) !== 0,
        hasValue: meta.get(2) !== 0,
        status:
          meta.get(2) !== 0
            ? {
                error: meta.get(3),
                position: values.get(0),
                speed: values.get(1),
                current: values.get(2),
                motorTemperature: values.get(3),
                mosTemperature: values.get(4),
              }
            : undefined,
      }
    } finally {
      values.release()
      meta.release()
    }
  }

  async pvtControl(command: PVTControlCommand): Promise<ControlResult> {
    if (this.runtime.backend.kind === 'native') return this.#invokeAsync('pvtControl', { ...command })
    if (command.feedback !== undefined && command.feedback !== (command.feedbackType ?? 0)) throw new Error('independent feedback selection is only supported by the native runtime')
    return this.#control(this.runtime.calls.motorPvtControl, [
      command.kp,
      command.kd,
      command.position,
      command.speed,
      command.torque,
      command.feedbackType ?? 0,
    ])
  }

  async spdControl(command: SpdControlCommand): Promise<ControlResult> {
    if (this.runtime.backend.kind === 'native') return this.#invokeAsync('spdControl', { ...command })
    if (command.feedback !== undefined && command.feedback !== (command.feedbackType ?? 0)) throw new Error('independent feedback selection is only supported by the native runtime')
    return this.#control(this.runtime.calls.motorSpdControl, [
      command.speed,
      command.current,
      command.feedbackType ?? 0,
    ])
  }

  async posControl(command: PosControlCommand): Promise<ControlResult> {
    if (this.runtime.backend.kind === 'native') return this.#invokeAsync('posControl', { ...command })
    if (command.feedback !== undefined && command.feedback !== (command.feedbackType ?? 0)) throw new Error('independent feedback selection is only supported by the native runtime')
    return this.#control(this.runtime.calls.motorPosControl, [
      command.position,
      command.speed,
      command.current,
      command.feedbackType ?? 0,
    ])
  }

  async curControl(command: CurrentControlCommand): Promise<ControlResult> {
    if (this.runtime.backend.kind === 'native') return this.#invokeAsync('curControl', { ...command })
    if (command.feedback !== undefined && command.feedback !== (command.feedbackType ?? 0)) throw new Error('independent feedback selection is only supported by the native runtime')
    return this.#control(this.runtime.calls.motorCurControl, [
      command.current,
      command.feedbackType ?? 0,
    ])
  }

  async setPosition(position: number): Promise<boolean> {
    this.#assertLive()
    if (this.runtime.backend.kind === 'native') return this.#invokeAsync('setPosition', { position })
    const out = allocU32()
    try {
      const code = await this.runtime.calls.motorSetPos(this.handle, position, out)
      assertOk(this.runtime, code, 'failed to set motor position')
      return getI32(out) !== 0
    } finally {
      out.release()
    }
  }

  async resetZeroPosition(waitForAck = true): Promise<boolean> {
    this.#assertLive()
    if (this.runtime.backend.kind === 'native') return this.#invokeAsync('resetZeroPosition', { waitForAck })
    const out = allocU32()
    try {
      const code = await this.runtime.calls.motorResetZeroPos(
        this.handle,
        waitForAck ? 1 : 0,
        out,
      )
      assertOk(this.runtime, code, 'failed to reset motor zero position')
      return getI32(out) !== 0
    } finally {
      out.release()
    }
  }

  async readPosition(): Promise<number> {
    return this.#readFloatParameter(MotorParameter.Position)
  }

  async readSpeed(): Promise<number> {
    return this.#readFloatParameter(MotorParameter.Speed)
  }

  async readCurrent(): Promise<number> {
    return this.#readFloatParameter(MotorParameter.Current)
  }

  async setCanTimeout(timeoutMs: number, waitForAck = true): Promise<boolean> {
    this.#assertLive()
    if (this.runtime.backend.kind === 'native') return this.#invokeAsync('setCanTimeout', { timeoutMs, waitForAck })
    const out = allocU32()
    try {
      const code = await this.runtime.calls.motorSetCanTimeout(
        this.handle,
        timeoutMs,
        waitForAck ? 1 : 0,
        out,
      )
      assertOk(this.runtime, code, 'failed to set CAN timeout')
      return getI32(out) !== 0
    } finally {
      out.release()
    }
  }

  async #control(fn: CFunction, args: number[]): Promise<ControlResult> {
    this.#assertLive()
    const values = new NumericBuffer('double', 5)
    const meta = new NumericBuffer('i32', 5)
    try {
      const code = await fn(this.handle, ...args, values, 5, meta, 5)
      assertOk(this.runtime, code, 'failed to control motor')
      const hasFeedback = meta.get(4) !== 0
      return {
        ok: meta.get(0) !== 0,
        feedbackType: meta.get(2),
        hasFeedback,
        noResponse: meta.get(3) === 255,
        feedback: hasFeedback
          ? {
              error: meta.get(3),
              position: values.get(0),
              speed: values.get(1),
              current: values.get(2),
              motorTemperature: values.get(3),
              mosTemperature: values.get(4),
            }
          : undefined,
      }
    } finally {
      values.release()
      meta.release()
    }
  }

  async #readFloatParameter(parameter: number): Promise<number> {
    this.#assertLive()
    if (this.runtime.backend.kind === 'native') return this.#invokeAsync('getParameter', { parameter })
    const out = new NumericBuffer('double', 1, false)
    try {
      const code = await this.runtime.calls.motorGetFloatParameter(this.handle, parameter, out)
      assertOk(this.runtime, code, 'failed to read motor parameter')
      return out.get(0)
    } finally {
      out.release()
    }
  }

  enableLog(baseName: string): void { this.#invoke('enableLog', { baseName }) }
  disableLog(): void { this.#invoke('disableLog') }
  isLogged(): boolean { return this.#invoke('isLogged') }
  getProperties(): MotorProperties { return this.#invoke('getProperties') }
  hasBrake(): boolean { return this.#invoke('hasBrake') }
  initMotorPvtParam(): Promise<void> { return this.#invokeAsync('initMotorPvtParam') }
  setProperties(properties: MotorProperties): void { this.#invoke('setProperties', { properties }) }
  /** @deprecated 使用 getProperties。 */
  getPvtRanges(): MotorProperties { return this.getProperties() }
  /** @deprecated 使用 setProperties。 */
  setDriverPvtRanges(properties: MotorProperties): void { this.setProperties(properties) }
  /** @deprecated 使用 setProperties 设置电流范围。 */
  setCurrentRange(range: number): void { this.#invoke('setCurrentRange', { range }) }
  enableCanFd(): void { this.#invoke('enableCanFd') }
  disableCanFd(): void { this.#invoke('disableCanFd') }
  isCanFdEnabled(): boolean { return this.#invoke('isCanFdEnabled') }
  enableCanEff(): void { this.#invoke('enableCanEff') }
  disableCanEff(): void { this.#invoke('disableCanEff') }
  isCanEffEnabled(): boolean { return this.#invoke('isCanEffEnabled') }
  async setId(newIndex: number, waitForAck = true): Promise<boolean> {
    const result = await this.#invokeAsync<boolean>('setId', { newIndex, waitForAck })
    if (result && waitForAck) {
      for (const [key, value] of this.bus.motors) if (value === this) this.bus.motors.delete(key)
      this.bus.motors.set(newIndex, this)
    }
    return result
  }
  setOnStatus(callback: StatusCallback<MotorStatus>): void {
    this.#assertLive()
    this.runtime._subscribe('motor.onStatus', { handle: this.handle }, callback)
  }
  setMaxStatusLifeCycle(maxLifeCycle: number): void { this.#invoke('setMaxStatusLifeCycle', { maxLifeCycle }) }
  setStatusMedianFilterWindowSize(windowSize: number): void { this.#invoke('setStatusMedianFilterWindowSize', { windowSize }) }
  setStatusLimitFilterMaxDeltas(deltas: MotorStatusDeltas): void { this.#invoke('setStatusLimitFilterMaxDeltas', { deltas }) }
  torControl(command: TorqueControlCommand): Promise<ControlResult> { return this.#invokeAsync('torControl', { ...command }) }
  stop(command: StopCommand): Promise<ControlResult> { return this.#invokeAsync('stop', { ...command }) }
  brake(enabled: boolean, waitForAck = true): Promise<boolean> { return this.#invokeAsync('brake', { enabled, waitForAck }) }
  setPvtKpRange(range: Range, waitForAck = true): Promise<boolean> { return this.#invokeAsync('setPvtKpRange', { range, waitForAck }) }
  setPvtKdRange(range: Range, waitForAck = true): Promise<boolean> { return this.#invokeAsync('setPvtKdRange', { range, waitForAck }) }
  setPvtPosRange(range: Range, waitForAck = true): Promise<boolean> { return this.#invokeAsync('setPvtPosRange', { range, waitForAck }) }
  setPvtSpdRange(range: Range, waitForAck = true): Promise<boolean> { return this.#invokeAsync('setPvtSpdRange', { range, waitForAck }) }
  setPvtTorRange(range: Range, waitForAck = true): Promise<boolean> { return this.#invokeAsync('setPvtTorRange', { range, waitForAck }) }
  setPvtCurRange(range: Range, waitForAck = true): Promise<boolean> { return this.#invokeAsync('setPvtCurRange', { range, waitForAck }) }
  setCurPi(gains: KpKi, waitForAck = true): Promise<boolean> { return this.#invokeAsync('setCurPi', { ...gains, waitForAck }) }
  setSpdPi(gains: KpKi, waitForAck = true): Promise<boolean> { return this.#invokeAsync('setSpdPi', { ...gains, waitForAck }) }
  setPosPd(gains: KpKd, waitForAck = true): Promise<boolean> { return this.#invokeAsync('setPosPd', { ...gains, waitForAck }) }
  setCommunicationMode(mode: number, waitForAck = true): Promise<boolean> { return this.#invokeAsync('setCommunicationMode', { mode, waitForAck }) }
  getParameter<P extends MotorParameterValue>(parameter: P): Promise<MotorParameterValues[P]> { return this.#invokeAsync('getParameter', { parameter }) }
  dispose(): void {
    if (this.disposed) return
    this.#invoke('dispose')
    this.disposed = true
    for (const [key, value] of this.bus.motors) if (value === this) this.bus.motors.delete(key)
  }
  #invoke<T>(name: string, args: Record<string, unknown> = {}): T {
    this.#assertLive()
    return this.runtime._invoke(`motor.${name}`, { handle: this.handle, ...args })
  }
  async #invokeAsync<T>(name: string, args: Record<string, unknown> = {}): Promise<T> {
    this.#assertLive()
    return this.runtime._invokeAsync(`motor.${name}`, { handle: this.handle, ...args })
  }

  #assertLive(): void {
    this.bus.adapter._assertLiveForChild()
    if (this.disposed || this.bus.disposed) {
      throw new Error('motor is disposed')
    }
  }
}
