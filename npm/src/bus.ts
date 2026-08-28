import { allocU32, assertOk, getU32 } from './native.js'
import type { AdapterWrapper } from './adapter.js'
import { BatteryWrapper } from './battery.js'
import { ImuWrapper } from './imu.js'
import { MotorWrapper } from './motor.js'

export class BusWrapper {
  adapter: AdapterWrapper
  runtime: AdapterWrapper['runtime']
  handle: number
  index: number
  disposed: boolean
  motors: Map<number, MotorWrapper>
  batteries: Map<number, BatteryWrapper>
  imus: Map<number, ImuWrapper>

  constructor(adapter: AdapterWrapper, handle: number, index: number) {
    this.adapter = adapter
    this.runtime = adapter.runtime
    this.handle = handle
    this.index = index
    this.disposed = false
    this.motors = new Map()
    this.batteries = new Map()
    this.imus = new Map()
  }

  getMotor(motorIndex: number, model: number): MotorWrapper {
    this.#assertLive()
    const cached = this.motors.get(motorIndex)
    if (cached) {
      return cached
    }
    const out = allocU32(this.runtime.module)
    try {
      const code = this.runtime.calls.busGetMotorWithModel(this.handle, motorIndex, model, out)
      assertOk(this.runtime, code, 'failed to get motor')
      const motor = new MotorWrapper(this, getU32(this.runtime.module, out), motorIndex)
      this.motors.set(motorIndex, motor)
      return motor
    } finally {
      this.runtime.module._free(out)
    }
  }

  getBattery(batteryIndex: number): BatteryWrapper {
    this.#assertLive()
    const cached = this.batteries.get(batteryIndex)
    if (cached) {
      return cached
    }
    const out = allocU32(this.runtime.module)
    try {
      const code = this.runtime.calls.busGetBattery(this.handle, batteryIndex, out)
      assertOk(this.runtime, code, 'failed to get battery')
      const battery = new BatteryWrapper(this, getU32(this.runtime.module, out), batteryIndex)
      this.batteries.set(batteryIndex, battery)
      return battery
    } finally {
      this.runtime.module._free(out)
    }
  }

  getImu(imuIndex: number): ImuWrapper {
    this.#assertLive()
    const cached = this.imus.get(imuIndex)
    if (cached) {
      return cached
    }
    const out = allocU32(this.runtime.module)
    try {
      const code = this.runtime.calls.busGetImu(this.handle, imuIndex, out)
      assertOk(this.runtime, code, 'failed to get IMU')
      const imu = new ImuWrapper(this, getU32(this.runtime.module, out), imuIndex)
      this.imus.set(imuIndex, imu)
      return imu
    } finally {
      this.runtime.module._free(out)
    }
  }

  #assertLive(): void {
    this.adapter._assertLiveForChild()
    if (this.disposed) {
      throw new Error('bus is disposed')
    }
  }
}
