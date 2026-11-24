// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import { allocU32, assertOk, getI32, getU32 } from './native.js'
import type { BusKey } from './types.js'
import type { Runtime } from './runtime.js'
import { BusWrapper } from './bus.js'
import { GloveWrapper } from './glove.js'
import type { MotorStatus, MotorStatusDeltas, StatusCallback } from './native-types.js'
import { FakeTools } from './fake-tools.js'

function busKeyFromRawIndex(rawIndex: number): BusKey {
  if (rawIndex > 0xff) {
    return {
      rawIndex,
      slaveIndex: rawIndex >> 16,
      busIndex: rawIndex & 0xff,
    }
  }
  return {
    rawIndex,
    slaveIndex: null,
    busIndex: rawIndex,
  }
}

export enum StatisticsStatus {
  Pending = 'pending',
  Valid = 'valid',
  Stale = 'stale',
}

export enum ErrorCounterKind {
  Current = 'current',
  LastErrorEvent = 'last-error-event',
}

export interface CanErrorCounters {
  tx: number
  rx: number

  kind: ErrorCounterKind
}

export interface BusStatistics {
  slaveId: number
  busId: number

  status: StatisticsStatus
  ageMs?: number

  errorCounters?: CanErrorCounters
}

export type CanBusStatistics = BusStatistics

export interface AdapterStatistics {
  buses: BusStatistics[]
}

export class AdapterWrapper {
  runtime: Runtime
  handle: number
  type: string
  interfaceName: string
  disposed: boolean
  gloves = new Map<number, GloveWrapper>()
  buses: Map<string, BusWrapper>

  constructor(runtime: Runtime, handle: number, type: string, interfaceName: string) {
    this.runtime = runtime
    this.handle = handle
    this.type = type
    this.interfaceName = interfaceName
    this.disposed = false
    this.buses = new Map()
  }

  ok(): boolean {
    this.#assertLive()
    const out = allocU32()
    try {
      const code = this.runtime.calls.adapterOk(this.handle, out)
      assertOk(this.runtime, code, 'failed to read adapter status')
      return getI32(out) !== 0
    } finally {
      out.release()
    }
  }

  listBusKeys(): BusKey[] {
    this.#assertLive()
    const out = allocU32()
    try {
      let code = this.runtime.calls.adapterGetBusCount(this.handle, out)
      assertOk(this.runtime, code, 'failed to list buses')
      const count = getI32(out)
      const keys: BusKey[] = []
      for (let index = 0; index < count; index += 1) {
        code = this.runtime.calls.adapterGetBusRawIndexAt(this.handle, index, out)
        assertOk(this.runtime, code, 'failed to list buses')
        keys.push(busKeyFromRawIndex(getI32(out)))
      }
      return keys
    } finally {
      out.release()
    }
  }

  getBus(first = 0, second: number | undefined = undefined): BusWrapper {
    this.#assertLive()
    const slaveIndex = second === undefined ? -1 : first
    const busIndex = second === undefined ? first : second
    const key = `${slaveIndex}:${busIndex}`
    const cached = this.buses.get(key)
    if (cached && this.runtime.backend.kind !== 'native') {
      return cached
    }
    if (this.runtime.backend.kind === 'native') {
      const handle = this.#invoke<number>('getBus', { busIndex, ...(second === undefined ? {} : { slaveIndex }) })
      if (cached && !cached.disposed && cached.handle === handle) return cached
      const bus = new BusWrapper(this, handle, busIndex)
      this.buses.set(key, bus)
      return bus
    }
    const out = allocU32()
    try {
      const code = this.runtime.calls.adapterGetBus(this.handle, slaveIndex, busIndex, out)
      assertOk(this.runtime, code, 'failed to get bus')
      const bus = new BusWrapper(this, getU32(out), busIndex)
      this.buses.set(key, bus)
      return bus
    } finally {
      out.release()
    }
  }

  dispose(): void {
    if (this.disposed) {
      return
    }
    const code = this.runtime.calls.disposeAdapter(this.handle)
    assertOk(this.runtime, code, 'failed to dispose adapter')

    this._invalidate()
  }

  /** @internal 原生层已完成销毁后，同步更新包装器状态。 */
  _invalidate(): void {
    this.disposed = true
    for (const bus of this.buses.values()) bus._invalidate()
    for (const glove of this.gloves.values()) glove.disposed = true
    this.gloves.clear()
    this.buses.clear()
    this.runtime.adapters.delete(this)
  }

  getBuses(): Map<number, BusWrapper> {
    const entries = this.#invoke<Array<{ index: number; handle: number }>>('getBuses')
    return new Map(entries.map(({ index, handle }) => {
      for (const bus of this.buses.values()) if (bus.handle === handle && !bus.disposed) return [index, bus] as const
      const bus = new BusWrapper(this, handle, index)
      this.buses.set(`-1:${index}`, bus)
      return [index, bus] as const
    }))
  }
  getGlove(slaveIndex: number): GloveWrapper {
    const handle = this.#invoke<number>('getGlove', { slaveIndex })
    const existing = this.gloves.get(slaveIndex)
    if (existing && !existing.disposed && existing.handle === handle) return existing
    const glove = new GloveWrapper(this, handle, slaveIndex)
    this.gloves.set(slaveIndex, glove)
    return glove
  }
  getFakeAdapterControl(): FakeTools | undefined {
    return this.#invoke<boolean>('hasFakeAdapterControl') ? new FakeTools(this) : undefined
  }
  updateBusExtend(extend: Record<string, unknown> | string): boolean { return this.#invoke('updateBusExtend', { extend }) }
  commit(): void { this.#invoke('commit') }
  setSyncMode(enabled: boolean): void { this.#invoke('setSyncMode', { enabled }) }

  getStatistics(): AdapterStatistics | null {
    this.#assertLive()

    if (this.runtime.backend.kind !== 'native') return null

    return this.#invoke('getStatistics')
  }

  getInterfaceName(): string { return this.#invoke('getInterfaceName') }
  getMotorStatus(): Map<bigint, MotorStatus>
  getMotorStatus(busIndex: number, motorIndex: number, lifeCycleDeduction?: number): MotorStatus | undefined
  getMotorStatus(busIndex?: number, motorIndex?: number, lifeCycleDeduction = 1): Map<bigint, MotorStatus> | MotorStatus | undefined {
    if (busIndex === undefined) {
      const entries = this.#invoke<Array<{ id: bigint; status: MotorStatus }>>('getMotorStatus')
      return new Map(entries.map(({ id, status }) => [BigInt(id), status]))
    }
    return this.#invoke<MotorStatus | null>('getMotorStatus', { busIndex, motorIndex, lifeCycleDeduction }) ?? undefined
  }
  setMaxStatusLifeCycle(maxLifeCycle: number): void { this.#invoke('setMaxStatusLifeCycle', { maxLifeCycle }) }
  getMaxStatusLifeCycle(): number { return this.#invoke('getMaxStatusLifeCycle') }
  setStatusMedianFilterWindowSize(windowSize: number): void { this.#invoke('setStatusMedianFilterWindowSize', { windowSize }) }
  setStatusLimitFilterMaxDeltas(deltas: MotorStatusDeltas): void { this.#invoke('setStatusLimitFilterMaxDeltas', { ...deltas }) }
  setOnStatus(busIndex: number, motorIndex: number, callback: StatusCallback<MotorStatus>): void {
    this.#assertLive()
    this.runtime._subscribe('adapter.onStatus', { handle: this.handle, busIndex, motorIndex }, callback)
  }
  #invoke<T>(name: string, args: Record<string, unknown> = {}): T {
    this.#assertLive()
    return this.runtime._invoke(`adapter.${name}`, { handle: this.handle, ...args })
  }

  _assertLiveForChild(): void {
    this.#assertLive()
  }

  #assertLive(): void {
    if (this.disposed) {
      throw new Error('adapter is disposed')
    }
  }
}

export class FakeAdapterWrapper extends AdapterWrapper {
  fake: FakeTools

  constructor(runtime: Runtime, handle: number, interfaceName: string) {
    super(runtime, handle, 'Fake', interfaceName)
    this.fake = new FakeTools(this)
  }
}
