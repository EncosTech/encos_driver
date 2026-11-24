// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import { allocU32, assertOk, getU32 } from './native.js'
import type { AdapterWrapper } from './adapter.js'
import { BatteryWrapper } from './battery.js'
import { ImuWrapper } from './imu.js'
import { PmsWrapper } from './pms.js'
import { DexHandWrapper } from './dex-hand.js'
import type { MotorOptions, MotorProperties } from './native-types.js'
import { MotorWrapper } from './motor.js'

export class BusWrapper {
  adapter: AdapterWrapper
  runtime: AdapterWrapper['runtime']
  handle: number
  #index: number
  get index(): number { return this.runtime.backend.kind === 'native' ? this.#invoke('getIndex') : this.#index }
  pms?: PmsWrapper
  disposed: boolean
  #pendingMotors = new Map<number, Promise<MotorWrapper>>()
  #dexHands = new Map<number, DexHandWrapper>()
  motors: Map<number, MotorWrapper>
  batteries: Map<number, BatteryWrapper>
  imus: Map<number, ImuWrapper>

  constructor(adapter: AdapterWrapper, handle: number, index: number) {
    this.adapter = adapter
    this.runtime = adapter.runtime
    this.handle = handle
    this.#index = index
    this.disposed = false
    this.motors = new Map()
    this.batteries = new Map()
    this.imus = new Map()
  }

  async getMotor(motorIndex: number, modelOrOptions?: number | boolean | MotorOptions | MotorProperties, canfd?: boolean): Promise<MotorWrapper> {
    this.#assertLive()
    const options: MotorOptions = typeof modelOrOptions === 'number' ? { model: modelOrOptions, canfd }
      : typeof modelOrOptions === 'boolean' ? { canfd: modelOrOptions }
      : modelOrOptions && 'kp' in modelOrOptions ? { properties: modelOrOptions, canfd }
      : { ...modelOrOptions, ...(canfd === undefined ? {} : { canfd }) }
    if (this.runtime.backend.kind === 'native') {
      const pending = this.#pendingMotors.get(motorIndex)
      if (pending) return pending
      const creation = this.#getNativeMotor(motorIndex, options)
      this.#pendingMotors.set(motorIndex, creation)
      try { return await creation } finally { this.#pendingMotors.delete(motorIndex) }
    }
    if (options.model === undefined || options.canfd !== undefined || options.properties !== undefined || options.frameFlags !== undefined) throw new Error('extended motor creation is only supported by the native runtime')
    const model = options.model
    const cached = this.motors.get(motorIndex)
    if (cached) {
      return cached
    }
    const pending = this.#pendingMotors.get(motorIndex)
    if (pending) {
      return pending
    }
    const creation = this.#createMotor(motorIndex, model)
    this.#pendingMotors.set(motorIndex, creation)
    try {
      return await creation
    } finally {
      this.#pendingMotors.delete(motorIndex)
    }
  }

  async #createMotor(motorIndex: number, model: number): Promise<MotorWrapper> {
    const out = allocU32()
    try {
      const code = await this.runtime.calls.busGetMotorWithModel(this.handle, motorIndex, model, out)
      this.#assertLive()
      assertOk(this.runtime, code, 'failed to get motor')
      const motor = new MotorWrapper(this, getU32(out), motorIndex)
      this.motors.set(motorIndex, motor)
      return motor
    } finally {
      out.release()
    }
  }

  getBattery(batteryIndex: number): BatteryWrapper {
    this.#assertLive()
    if (this.runtime.backend.kind === 'native') {
      const handle = this.#invoke<number>('getBattery', { index: batteryIndex })
      const existing = this.batteries.get(batteryIndex)
      if (existing && !existing.disposed && existing.handle === handle) return existing
      const wrapper = new BatteryWrapper(this, handle, batteryIndex)
      this.batteries.set(batteryIndex, wrapper)
      return wrapper
    }
    const cached = this.batteries.get(batteryIndex)
    if (cached) {
      return cached
    }
    const out = allocU32()
    try {
      const code = this.runtime.calls.busGetBattery(this.handle, batteryIndex, out)
      assertOk(this.runtime, code, 'failed to get battery')
      const battery = new BatteryWrapper(this, getU32(out), batteryIndex)
      this.batteries.set(batteryIndex, battery)
      return battery
    } finally {
      out.release()
    }
  }

  getImu(imuIndex: number): ImuWrapper {
    this.#assertLive()
    if (this.runtime.backend.kind === 'native') {
      const handle = this.#invoke<number>('getImu', { index: imuIndex })
      const existing = this.imus.get(imuIndex)
      if (existing && !existing.disposed && existing.handle === handle) return existing
      const wrapper = new ImuWrapper(this, handle, imuIndex)
      this.imus.set(imuIndex, wrapper)
      return wrapper
    }
    const cached = this.imus.get(imuIndex)
    if (cached) {
      return cached
    }
    const out = allocU32()
    try {
      const code = this.runtime.calls.busGetImu(this.handle, imuIndex, out)
      assertOk(this.runtime, code, 'failed to get IMU')
      const imu = new ImuWrapper(this, getU32(out), imuIndex)
      this.imus.set(imuIndex, imu)
      return imu
    } finally {
      out.release()
    }
  }

  async #getNativeMotor(index: number, options: MotorOptions): Promise<MotorWrapper> {
    const handle = await this.#invokeAsync<number>('getMotor', { index, ...options })
    this.#assertLive()
    return this.#wrapMotor(index, handle)
  }
  #wrapMotor(index: number, handle: number): MotorWrapper {
    const cached = this.motors.get(index)
    if (cached && !cached.disposed && cached.handle === handle) return cached
    for (const [oldIndex, motor] of this.motors) {
      if (motor.handle === handle && !motor.disposed) {
        this.motors.delete(oldIndex)
        this.motors.set(index, motor)
        return motor
      }
    }
    const motor = new MotorWrapper(this, handle, index)
    this.motors.set(index, motor)
    return motor
  }
  selectMotor(index: number): MotorWrapper | undefined {
    const handle = this.#invoke<number | null>('selectMotor', { index })
    return handle === null ? undefined : this.#wrapMotor(index, handle)
  }
  getMotors(): Map<number, MotorWrapper> {
    return this.#motorMap(this.#invoke('getMotors'))
  }
  async scanMotors(): Promise<Map<number, MotorWrapper>> {
    const entries = await this.#invokeAsync<Array<{ index: number; handle: number }>>('scanMotors')
    this.#assertLive()
    return this.#motorMap(entries)
  }
  #motorMap(entries: Array<{ index: number; handle: number }>): Map<number, MotorWrapper> {
    return new Map(entries.map(({ index, handle }) => [index, this.#wrapMotor(index, handle)]))
  }
  getPms(): PmsWrapper {
    const handle = this.#invoke<number>('getPms')
    if (!this.pms || this.pms.disposed || this.pms.handle !== handle) this.pms = new PmsWrapper(this, handle)
    return this.pms
  }
  /** 获取指定基址的灵巧手网关，单网关时可以使用广播地址 0。 */
  getDexHand(baseId = 1): DexHandWrapper {
    const handle = this.#invoke<number>('getDexHand', { baseId })
    const cached = this.#dexHands.get(baseId)
    if (cached && !cached.disposed && cached.handle === handle) return cached
    const hand = new DexHandWrapper(this, handle, baseId)
    this.#dexHands.set(baseId, hand)
    return hand
  }
  startImu(): boolean { return this.#invoke('startImu') }
  stopImu(): boolean { return this.#invoke('stopImu') }
  detectExternalDevice(): Promise<boolean> { return this.#invokeAsync('detectExternalDevice') }
  hasExternalDevice(): boolean { return this.#invoke('hasExternalDevice') }
  resetMotorsId(waitForAck = true): Promise<boolean> { return this.#invokeAsync('resetMotorsId', { waitForAck }) }
  startAutoAssignCanId(startIndex: number): boolean { return this.#invoke('startAutoAssignCanId', { startIndex }) }
  stopAutoAssignCanId(): boolean { return this.#invoke('stopAutoAssignCanId') }
  commit(): void { this.#invoke('commit') }
  setSyncMode(enabled: boolean): void { this.#invoke('setSyncMode', { enabled }) }
  getBusIndex(): number { return this.index }
  dispose(): void {
    if (this.disposed) return
    this.#invoke('dispose')
    this._invalidate()
    for (const [key, value] of this.adapter.buses) if (value === this) this.adapter.buses.delete(key)
  }
  /** @internal 父对象销毁后使所有子包装器失效。 */
  _invalidate(): void {
    this.disposed = true
    for (const motor of this.motors.values()) motor.disposed = true
    for (const battery of this.batteries.values()) battery.disposed = true
    for (const imu of this.imus.values()) imu.disposed = true
    if (this.pms) this.pms.disposed = true
    for (const hand of this.#dexHands.values()) hand.disposed = true
    this.#dexHands.clear()
  }
  #invoke<T>(name: string, args: Record<string, unknown> = {}): T {
    this.#assertLive()
    return this.runtime._invoke(`bus.${name}`, { handle: this.handle, ...args })
  }
  async #invokeAsync<T>(name: string, args: Record<string, unknown> = {}): Promise<T> {
    this.#assertLive()
    return this.runtime._invokeAsync(`bus.${name}`, { handle: this.handle, ...args })
  }

  #assertLive(): void {
    this.adapter._assertLiveForChild()
    if (this.disposed) {
      throw new Error('bus is disposed')
    }
  }
}
