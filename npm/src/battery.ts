import { assertOk } from './native.js'
import type { BusWrapper } from './bus.js'
import type { BatteryPassiveCommands, BatteryStatusResult } from './types.js'

export class BatteryWrapper {
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

  getStatus(): BatteryStatusResult {
    this.#assertLive()
    const values = this.runtime.module._malloc(27 * 8)
    const meta = this.runtime.module._malloc(3 * 4)
    try {
      const code = this.runtime.calls.batteryGetStatus(this.handle, values, 27, meta, 3)
      assertOk(this.runtime, code, 'failed to get battery status')
      const hasState = this.runtime.module.getValue(meta, 'i32') !== 0
      const hasTemp = this.runtime.module.getValue(meta + 4, 'i32') !== 0
      const hasActiveCommands = this.runtime.module.getValue(meta + 8, 'i32') !== 0
      return {
        state: hasState
          ? {
              isMaster: this.#doubleAt(values, 0) !== 0,
              soc: this.#doubleAt(values, 1),
              voltage: this.#doubleAt(values, 2),
              allowedDischargeCurrent: this.#doubleAt(values, 3),
              allowedChargeCurrent: this.#doubleAt(values, 4),
            }
          : undefined,
        temp: hasTemp
          ? {
              battery: this.#doubleAt(values, 5),
              mos: this.#doubleAt(values, 6),
              dischargeCurrent: this.#doubleAt(values, 7),
              chargeCurrent: this.#doubleAt(values, 8),
            }
          : undefined,
        error: {
          couldNotCharge: this.#doubleAt(values, 9) !== 0,
          couldNotDischarge: this.#doubleAt(values, 10) !== 0,
          lowBattery: this.#doubleAt(values, 11) !== 0,
          overCurrentSteady: this.#doubleAt(values, 12) !== 0,
          overCurrentPeak: this.#doubleAt(values, 13) !== 0,
          overCurrentCharge: this.#doubleAt(values, 14) !== 0,
          batteryOverTemp: this.#doubleAt(values, 15) !== 0,
          mosOverTemp: this.#doubleAt(values, 16) !== 0,
          couldNotCommunicate: this.#doubleAt(values, 17) !== 0,
          stoppedEmergency: this.#doubleAt(values, 18) !== 0,
          chargerFault: this.#doubleAt(values, 19) !== 0,
          commTimeout: this.#doubleAt(values, 20) !== 0,
        },
        activeCommands: hasActiveCommands
          ? {
              shutdownRequest: this.#doubleAt(values, 21) !== 0,
              dischargeRequest: this.#doubleAt(values, 22) !== 0,
              forceShutdownBroadcast: this.#doubleAt(values, 23) !== 0,
              allowCharging: this.#doubleAt(values, 24) !== 0,
              faultShutdownBroadcast: this.#doubleAt(values, 25) !== 0,
              mosStatus: this.#doubleAt(values, 26) !== 0,
            }
          : undefined,
      }
    } finally {
      this.runtime.module._free(values)
      this.runtime.module._free(meta)
    }
  }

  sendPassiveCommands(commands: BatteryPassiveCommands): void {
    this.#assertLive()
    let byte0 = 0
    let byte1 = 0
    byte0 |= commands.allowShutdown ? 1 << 0 : 0
    byte0 |= commands.allowDischarge ? 1 << 1 : 0
    byte0 |= commands.parallelDischarge ? 1 << 2 : 0
    byte0 |= commands.forceShutdown ? 1 << 3 : 0
    byte0 |= commands.requestCharging ? 1 << 4 : 0
    byte0 |= commands.faultShutdownBroadcast ? 1 << 5 : 0
    byte0 |= commands.configureFaultThresholds ? 1 << 6 : 0
    byte0 |= commands.clearFault ? 1 << 7 : 0
    byte1 |= commands.factoryMode ? 1 << 0 : 0
    byte1 |= commands.debug ? 1 << 1 : 0
    const code = this.runtime.calls.batterySendPassiveCommands(this.handle, byte0, byte1)
    assertOk(this.runtime, code, 'failed to send battery passive commands')
  }

  clearFault(): void {
    this.#assertLive()
    const code = this.runtime.calls.batteryClearFault(this.handle)
    assertOk(this.runtime, code, 'failed to clear battery fault')
  }

  requestCharging(enabled: boolean): void {
    this.#assertLive()
    const code = this.runtime.calls.batteryRequestCharging(this.handle, enabled ? 1 : 0)
    assertOk(this.runtime, code, 'failed to set battery charging request')
  }

  allowDischarge(enabled: boolean): void {
    this.#assertLive()
    const code = this.runtime.calls.batteryAllowDischarge(this.handle, enabled ? 1 : 0)
    assertOk(this.runtime, code, 'failed to set battery discharge permission')
  }

  #assertLive(): void {
    this.bus.adapter._assertLiveForChild()
    if (this.disposed || this.bus.disposed) {
      throw new Error('battery is disposed')
    }
  }

  #doubleAt(ptr: number, index: number): number {
    return this.runtime.module.getValue(ptr + index * 8, 'double')
  }
}
