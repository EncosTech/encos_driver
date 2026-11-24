// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import type { CFunction, WasmModule } from './types.js'

/** 调用期间使用的数值数组；仅 WASM 后端将其转换为线性内存。 */
export class NumericBuffer {
  values: number[]
  constructor(
    readonly type: 'i8' | 'i32' | 'double',
    readonly length: number,
    readonly span = true,
  ) {
    this.values = Array(length).fill(0)
  }
  get(index: number): number { return this.values[index] }
  set(index: number, value: number): void { this.values[index] = value }
  release(): void { this.values = [] }
}

export interface RuntimeBackend {
  readonly kind: 'wasm' | 'native'
  calls: Record<string, CFunction>
  readonly module?: WasmModule
  lastError: string
  invoke?<T>(name: string, args: Record<string, unknown>): T
  invokeAsync?<T>(name: string, args: Record<string, unknown>): Promise<T>
  subscribe?(name: string, args: Record<string, unknown>, callback: ((value: any) => void) | null): void
  dispose?(): void
}
