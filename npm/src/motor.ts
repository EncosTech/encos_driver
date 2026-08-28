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
import { MotorParameter } from './constants.js'

export class MotorWrapper {
  bus: BusWrapper
  runtime: BusWrapper['runtime']
  handle: number
  index: number
  disposed: boolean

  constructor(bus: BusWrapper, handle: number, index: number) {
    this.bus = bus
    this.runtime = bus.runtime
    this.handle = handle
    this.index = index
    this.disposed = false
  }

  getStatus(): StatusResult {
    this.#assertLive()
    const values = this.runtime.module._malloc(5 * 8)
    const meta = this.runtime.module._malloc(4 * 4)
    try {
      const code = this.runtime.calls.motorGetStatus(this.handle, 1, values, 5, meta, 4)
      assertOk(this.runtime, code, 'failed to get motor status')
      return {
        ok: this.runtime.module.getValue(meta, 'i32') !== 0,
        hasValue: this.runtime.module.getValue(meta + 8, 'i32') !== 0,
        status:
          this.runtime.module.getValue(meta + 8, 'i32') !== 0
            ? {
                error: this.runtime.module.getValue(meta + 12, 'i32'),
                position: this.runtime.module.getValue(values, 'double'),
                speed: this.runtime.module.getValue(values + 8, 'double'),
                current: this.runtime.module.getValue(values + 16, 'double'),
                motorTemperature: this.runtime.module.getValue(values + 24, 'double'),
                mosTemperature: this.runtime.module.getValue(values + 32, 'double'),
              }
            : undefined,
      }
    } finally {
      this.runtime.module._free(values)
      this.runtime.module._free(meta)
    }
  }

  async pvtControl(command: PVTControlCommand): Promise<ControlResult> {
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
    return this.#control(this.runtime.calls.motorSpdControl, [
      command.speed,
      command.current,
      command.feedbackType ?? 0,
    ])
  }

  async posControl(command: PosControlCommand): Promise<ControlResult> {
    return this.#control(this.runtime.calls.motorPosControl, [
      command.position,
      command.speed,
      command.current,
      command.feedbackType ?? 0,
    ])
  }

  async curControl(command: CurrentControlCommand): Promise<ControlResult> {
    return this.#control(this.runtime.calls.motorCurControl, [
      command.current,
      command.feedbackType ?? 0,
    ])
  }

  async setPosition(position: number): Promise<boolean> {
    this.#assertLive()
    const out = allocU32(this.runtime.module)
    try {
      const code = await this.runtime.calls.motorSetPos(this.handle, position, out)
      assertOk(this.runtime, code, 'failed to set motor position')
      return getI32(this.runtime.module, out) !== 0
    } finally {
      this.runtime.module._free(out)
    }
  }

  async resetZeroPosition(waitForAck = true): Promise<boolean> {
    this.#assertLive()
    const out = allocU32(this.runtime.module)
    try {
      const code = await this.runtime.calls.motorResetZeroPos(
        this.handle,
        waitForAck ? 1 : 0,
        out,
      )
      assertOk(this.runtime, code, 'failed to reset motor zero position')
      return getI32(this.runtime.module, out) !== 0
    } finally {
      this.runtime.module._free(out)
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
    const out = allocU32(this.runtime.module)
    try {
      const code = await this.runtime.calls.motorSetCanTimeout(
        this.handle,
        timeoutMs,
        waitForAck ? 1 : 0,
        out,
      )
      assertOk(this.runtime, code, 'failed to set CAN timeout')
      return getI32(this.runtime.module, out) !== 0
    } finally {
      this.runtime.module._free(out)
    }
  }

  async #control(fn: CFunction, args: number[]): Promise<ControlResult> {
    this.#assertLive()
    const values = this.runtime.module._malloc(5 * 8)
    const meta = this.runtime.module._malloc(5 * 4)
    try {
      const code = await fn(this.handle, ...args, values, 5, meta, 5)
      assertOk(this.runtime, code, 'failed to control motor')
      const hasFeedback = this.runtime.module.getValue(meta + 16, 'i32') !== 0
      return {
        ok: this.runtime.module.getValue(meta, 'i32') !== 0,
        feedbackType: this.runtime.module.getValue(meta + 8, 'i32'),
        hasFeedback,
        noResponse: this.runtime.module.getValue(meta + 12, 'i32') === 255,
        feedback: hasFeedback
          ? {
              error: this.runtime.module.getValue(meta + 12, 'i32'),
              position: this.runtime.module.getValue(values, 'double'),
              speed: this.runtime.module.getValue(values + 8, 'double'),
              current: this.runtime.module.getValue(values + 16, 'double'),
              motorTemperature: this.runtime.module.getValue(values + 24, 'double'),
              mosTemperature: this.runtime.module.getValue(values + 32, 'double'),
            }
          : undefined,
      }
    } finally {
      this.runtime.module._free(values)
      this.runtime.module._free(meta)
    }
  }

  async #readFloatParameter(parameter: number): Promise<number> {
    this.#assertLive()
    const out = this.runtime.module._malloc(8)
    try {
      const code = await this.runtime.calls.motorGetFloatParameter(this.handle, parameter, out)
      assertOk(this.runtime, code, 'failed to read motor parameter')
      return this.runtime.module.getValue(out, 'double')
    } finally {
      this.runtime.module._free(out)
    }
  }

  #assertLive(): void {
    this.bus.adapter._assertLiveForChild()
    if (this.disposed || this.bus.disposed) {
      throw new Error('motor is disposed')
    }
  }
}
