import { allocU32, assertOk, getI32, getU32 } from './native.js'
import type { BusKey } from './types.js'
import type { Runtime } from './runtime.js'
import { BusWrapper } from './bus.js'
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

export class AdapterWrapper {
  runtime: Runtime
  handle: number
  type: string
  interfaceName: string
  disposed: boolean
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
    const out = allocU32(this.runtime.module)
    try {
      const code = this.runtime.calls.adapterOk(this.handle, out)
      assertOk(this.runtime, code, 'failed to read adapter status')
      return getI32(this.runtime.module, out) !== 0
    } finally {
      this.runtime.module._free(out)
    }
  }

  listBusKeys(): BusKey[] {
    this.#assertLive()
    const out = allocU32(this.runtime.module)
    try {
      let code = this.runtime.calls.adapterGetBusCount(this.handle, out)
      assertOk(this.runtime, code, 'failed to list buses')
      const count = getI32(this.runtime.module, out)
      const keys: BusKey[] = []
      for (let index = 0; index < count; index += 1) {
        code = this.runtime.calls.adapterGetBusRawIndexAt(this.handle, index, out)
        assertOk(this.runtime, code, 'failed to list buses')
        keys.push(busKeyFromRawIndex(getI32(this.runtime.module, out)))
      }
      return keys
    } finally {
      this.runtime.module._free(out)
    }
  }

  getBus(first = 0, second: number | undefined = undefined): BusWrapper {
    this.#assertLive()
    const slaveIndex = second === undefined ? -1 : first
    const busIndex = second === undefined ? first : second
    const key = `${slaveIndex}:${busIndex}`
    const cached = this.buses.get(key)
    if (cached) {
      return cached
    }
    const out = allocU32(this.runtime.module)
    try {
      const code = this.runtime.calls.adapterGetBus(this.handle, slaveIndex, busIndex, out)
      assertOk(this.runtime, code, 'failed to get bus')
      const bus = new BusWrapper(this, getU32(this.runtime.module, out), busIndex)
      this.buses.set(key, bus)
      return bus
    } finally {
      this.runtime.module._free(out)
    }
  }

  dispose(): void {
    if (this.disposed) {
      return
    }
    const code = this.runtime.calls.disposeAdapter(this.handle)
    assertOk(this.runtime, code, 'failed to dispose adapter')

    this.disposed = true
    for (const bus of this.buses.values()) {
      bus.disposed = true
      for (const motor of bus.motors.values()) {
        motor.disposed = true
      }
      for (const battery of bus.batteries.values()) {
        battery.disposed = true
      }
      for (const imu of bus.imus.values()) {
        imu.disposed = true
      }
    }
    this.buses.clear()
    this.runtime.adapters.delete(this)
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
