// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import type { Runtime } from './runtime.js'
import type { AdapterWrapper } from './adapter.js'
import type { BusWrapper } from './bus.js'
import type { MotorWrapper } from './motor.js'
import type { BatteryWrapper } from './battery.js'
import type { ImuWrapper } from './imu.js'
import type { PmsWrapper } from './pms.js'
import type { DexHandWrapper } from './dex-hand.js'
import type { GloveWrapper } from './glove.js'
import type { CreateAdapterOptions } from './types.js'
import type { MotorOptions, MotorPackMsg } from './native-types.js'

/** 当前原生运行时范围内的设备创建、查询与销毁入口。 */
export class DriverManager {
  constructor(readonly runtime: Runtime) {}
  #owned<T extends { runtime: Runtime }>(object: T): T {
    if (object.runtime !== this.runtime) throw new Error('object belongs to another runtime')
    return object
  }
  createAdapter<T extends string>(options: CreateAdapterOptions<T>) { return this.runtime.createAdapter(options) }
  createBus(adapter: AdapterWrapper, rawBusIndex: number): BusWrapper { return this.#owned(adapter).getBus(rawBusIndex) }
  createMotor(bus: BusWrapper, index: number, options: MotorOptions = {}): Promise<MotorWrapper> { return this.#owned(bus).getMotor(index, options) }
  createBattery(bus: BusWrapper, index: number): BatteryWrapper { return this.#owned(bus).getBattery(index) }
  createImu(bus: BusWrapper, index: number): ImuWrapper { return this.#owned(bus).getImu(index) }
  createPms(bus: BusWrapper): PmsWrapper { return this.#owned(bus).getPms() }
  createDexHand(bus: BusWrapper, baseId = 1): DexHandWrapper { return this.#owned(bus).getDexHand(baseId) }
  createGlove(adapter: AdapterWrapper, slaveIndex: number): GloveWrapper { return this.#owned(adapter).getGlove(slaveIndex) }
  findMotor(bus: BusWrapper, index: number): MotorWrapper | undefined { return this.#owned(bus).selectMotor(index) }
  getMotors(bus: BusWrapper): Map<number, MotorWrapper> { return this.#owned(bus).getMotors() }
  getBuses(adapter: AdapterWrapper): Map<number, BusWrapper> { return this.#owned(adapter).getBuses() }
  migrateMotorIndex(motor: MotorWrapper, index: number): boolean {
    this.#owned(motor)
    return this.runtime._invoke('manager.migrateMotorIndex', { handle: motor.handle, index })
  }
  destroyAdapter(adapter: AdapterWrapper): boolean { return this.#destroy(adapter) }
  destroyBus(bus: BusWrapper): boolean { return this.#destroy(bus) }
  destroyMotor(motor: MotorWrapper): boolean { return this.#destroy(motor) }
  destroyBattery(battery: BatteryWrapper): boolean { return this.#destroy(battery) }
  destroyImu(imu: ImuWrapper): boolean { return this.#destroy(imu) }
  destroyPms(pms: PmsWrapper): boolean { return this.#destroy(pms) }
  destroyDexHand(hand: DexHandWrapper): boolean { return this.#destroy(hand) }
  destroyGlove(glove: GloveWrapper): boolean { return this.#destroy(glove) }
  #destroy(object: { runtime: Runtime; disposed: boolean; dispose(): void }): boolean {
    this.#owned(object)
    if (object.disposed) return false
    object.dispose()
    return true
  }
  destroyAdapterByInterfaceName(interfaceName: string): boolean {
    const destroyed = this.runtime._invoke<boolean>('manager.destroyAdapterByInterfaceName', { interfaceName })
    if (destroyed) {
      for (const adapter of Array.from(this.runtime.adapters)) {
        if (adapter.interfaceName === interfaceName) adapter._invalidate()
      }
    }
    return destroyed
  }
  dispatchReceive(adapter: AdapterWrapper, busIndex: number, message: MotorPackMsg): boolean {
    this.#owned(adapter)
    return this.runtime._invoke('manager.dispatchReceive', { handle: adapter.handle, busIndex, message })
  }
  dispatchUnknownReceive(adapter: AdapterWrapper, busIndex: number, message: MotorPackMsg): boolean {
    this.#owned(adapter)
    return this.runtime._invoke('manager.dispatchUnknownReceive', { handle: adapter.handle, busIndex, message })
  }
  makeReceiveUniqueId(busIndex: number, canId: number): bigint {
    return BigInt(this.runtime._invoke<number | bigint>('manager.makeReceiveUniqueId', { busIndex, canId }))
  }
}
