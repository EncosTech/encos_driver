// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import type { AdapterWrapper } from './adapter.js'
import type { GloveStatus, GloveCalibrationStatusValue, StatusCallback } from './native-types.js'
function normalizeStatus(value: (number | null)[][]): GloveStatus {
  return value.map(row => row.map(item => item ?? undefined)) as GloveStatus
}
export class GloveWrapper {
  disposed = false
  constructor(readonly adapter: AdapterWrapper, readonly handle: number, readonly slaveIndex: number) {}
  get runtime() { return this.adapter.runtime }
  getStatus(): GloveStatus {
    this.#assertLive()
    return normalizeStatus(this.runtime._invoke('glove.getStatus', { handle: this.handle }))
  }
  setOnStatus(callback: StatusCallback<GloveStatus>): void {
    this.#assertLive()
    this.runtime._subscribe('glove.onStatus', { handle: this.handle }, callback ? value => callback(normalizeStatus(value)) : null)
  }
  async calibrateAll(): Promise<GloveCalibrationStatusValue> {
    this.#assertLive()
    return this.runtime._invokeAsync('glove.calibrateAll', { handle: this.handle })
  }
  async calibrateByMask(fingerIndex: number, encoderMask: number): Promise<GloveCalibrationStatusValue> {
    this.#assertLive()
    return this.runtime._invokeAsync('glove.calibrateByMask', { handle: this.handle, fingerIndex, encoderMask })
  }
  dispose(): void {
    if (this.disposed) return
    this.#assertLive()
    this.runtime._invoke('glove.dispose', { handle: this.handle })
    this.disposed = true
    this.adapter.gloves.delete(this.slaveIndex)
  }
  #assertLive(): void {
    this.adapter._assertLiveForChild()
    if (this.disposed) throw new Error('glove is disposed')
  }
}
