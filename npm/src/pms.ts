// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import type { BusWrapper } from './bus.js'
import type { PmsStatus, StatusCallback } from './native-types.js'
export class PmsWrapper {
  disposed = false
  constructor(readonly bus: BusWrapper, readonly handle: number) {}
  get runtime() { return this.bus.runtime }
  getStatus(): PmsStatus | undefined { return this.#invoke<PmsStatus | null>('getStatus') ?? undefined }
  sendCommand(command: number): void { this.#invoke('sendCommand', { command }) }
  setOnStatus(callback: StatusCallback<PmsStatus>): void {
    this.#assertLive()
    this.runtime._subscribe('pms.onStatus', { handle: this.handle }, callback)
  }
  dispose(): void {
    if (this.disposed) return
    this.#invoke('dispose')
    this.disposed = true
    this.bus.pms = undefined
  }
  #assertLive(): void {
    this.bus.adapter._assertLiveForChild()
    if (this.disposed || this.bus.disposed) throw new Error('PMS is disposed')
  }
  #invoke<T>(name: string, args: Record<string, unknown> = {}): T {
    this.#assertLive()
    return this.runtime._invoke(`pms.${name}`, { handle: this.handle, ...args })
  }
}
