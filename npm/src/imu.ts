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
    const values = this.runtime.module._malloc(13 * 8)
    const present = this.runtime.module._malloc(4 * 4)
    try {
      const code = this.runtime.calls.imuGetStatus(this.handle, values, 13, present, 4)
      assertOk(this.runtime, code, 'failed to get IMU status')
      const hasAcceleration = this.runtime.module.getValue(present, 'i32') !== 0
      const hasAngularVelocity = this.runtime.module.getValue(present + 4, 'i32') !== 0
      const hasEulerAngle = this.runtime.module.getValue(present + 8, 'i32') !== 0
      const hasQuaternion = this.runtime.module.getValue(present + 12, 'i32') !== 0
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
      this.runtime.module._free(values)
      this.runtime.module._free(present)
    }
  }

  #assertLive(): void {
    this.bus.adapter._assertLiveForChild()
    if (this.disposed || this.bus.disposed) {
      throw new Error('IMU is disposed')
    }
  }

  #doubleAt(ptr: number, index: number): number {
    return this.runtime.module.getValue(ptr + index * 8, 'double')
  }
}
