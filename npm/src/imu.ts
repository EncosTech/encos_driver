// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import type { ImuStatus, StatusCallback } from './native-types.js'
import { NumericBuffer } from './backend.js'
import { assertOk } from './native.js'
import type { BusWrapper } from './bus.js'
import type { ImuStatusResult } from './types.js'

export class ImuWrapper {
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

  getStatus(): ImuStatusResult {
    this.#assertLive()
    const values = new NumericBuffer('double', 13)
    const present = new NumericBuffer('i32', 4)
    try {
      const code = this.runtime.calls.imuGetStatus(this.handle, values, 13, present, 4)
      assertOk(this.runtime, code, 'failed to get IMU status')
      const hasAcceleration = present.get(0) !== 0
      const hasAngularVelocity = present.get(1) !== 0
      const hasEulerAngle = present.get(2) !== 0
      const hasQuaternion = present.get(3) !== 0
      return {
        acceleration: hasAcceleration
          ? {
              x: this.#doubleAt(values, 0),
              y: this.#doubleAt(values, 1),
              z: this.#doubleAt(values, 2),
            }
          : undefined,
        angularVelocity: hasAngularVelocity
          ? {
              x: this.#doubleAt(values, 3),
              y: this.#doubleAt(values, 4),
              z: this.#doubleAt(values, 5),
            }
          : undefined,
        eulerAngle: hasEulerAngle
          ? {
              pitch: this.#doubleAt(values, 6),
              roll: this.#doubleAt(values, 7),
              heading: this.#doubleAt(values, 8),
            }
          : undefined,
        quaternion: hasQuaternion
          ? {
              qw: this.#doubleAt(values, 9),
              qx: this.#doubleAt(values, 10),
              qy: this.#doubleAt(values, 11),
              qz: this.#doubleAt(values, 12),
            }
          : undefined,
      }
    } finally {
      values.release()
      present.release()
    }
  }

  setOnStatus(callback: StatusCallback<ImuStatus>): void {
    this.#assertLive()
    this.runtime._subscribe('imu.onStatus', { handle: this.handle }, callback)
  }
  dispose(): void {
    if (this.disposed) return
    this.#assertLive()
    this.runtime._invoke('imu.dispose', { handle: this.handle })
    this.disposed = true
    this.bus.imus.delete(this.index)
  }
  static nativeUpdateInterval(): number { return 5 }
  static jsUpdateInterval(): number { return 20 }

  #assertLive(): void {
    this.bus.adapter._assertLiveForChild()
    if (this.disposed || this.bus.disposed) {
      throw new Error('IMU is disposed')
    }
  }

  #doubleAt(ptr: NumericBuffer, index: number): number {
    return ptr.get(index)
  }
}
