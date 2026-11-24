// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import type { BusWrapper } from './bus.js'
import type { DexHandReply, DexHandRequestOptions, DexHandSamplePoints } from './native-types.js'

/** 绑定到总线和请求基址的灵巧手 CAN 管理设备。 */
export class DexHandWrapper {
  disposed = false

  constructor(readonly bus: BusWrapper, readonly handle: number, readonly baseId: number) {}

  get runtime() { return this.bus.runtime }

  getBaseId(options?: DexHandRequestOptions): Promise<DexHandReply<number>> {
    return this.#invoke('getBaseId', { ...options })
  }

  /** 成功后通过新基址重新获取网关和子电机。 */
  setBaseId(baseId: number, options?: DexHandRequestOptions): Promise<DexHandReply<number>> {
    return this.#invoke('setBaseId', { ...options, baseId })
  }

  getSamplePoints(options?: DexHandRequestOptions): Promise<DexHandReply<DexHandSamplePoints>> {
    return this.#invoke('getSamplePoints', { ...options })
  }

  /** 新采样点在网关重启后生效。 */
  setSamplePoints(points: DexHandSamplePoints, options?: DexHandRequestOptions): Promise<DexHandReply<DexHandSamplePoints>> {
    return this.#invoke('setSamplePoints', { ...options, points })
  }

  dispose(): void {
    if (this.disposed) return
    this.#assertLive()
    this.runtime._invoke('dexHand.dispose', { handle: this.handle })
    this.disposed = true
  }

  #assertLive(): void {
    this.bus.adapter._assertLiveForChild()
    if (this.disposed || this.bus.disposed) throw new Error('DexHand is disposed')
  }

  async #invoke<T>(operation: string, args: Record<string, unknown>): Promise<DexHandReply<T>> {
    this.#assertLive()
    const reply = await this.runtime._invokeAsync<DexHandReply<T>>(`dexHand.${operation}`, { ...args, handle: this.handle })
    this.#assertLive()
    return reply
  }
}
