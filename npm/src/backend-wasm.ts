// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import { NumericBuffer, type RuntimeBackend } from './backend.js'
import { createRuntimeCalls } from './native.js'
import type { WasmModule } from './types.js'

export function createWasmBackend(module: WasmModule): RuntimeBackend {
  let active = false
  const backend: RuntimeBackend = { kind: 'wasm', module, calls: {}, lastError: '' }
  const wrap: WasmModule['cwrap'] = (name, returnType, argTypes, options) => {
    const fn = module.cwrap(name, returnType, argTypes, options)
    return (...args: unknown[]) => {
      if (options?.async && active) {
        return Promise.reject(new Error('another async operation is in progress; await it before starting a new one'))
      }
      const buffers: Array<{ buffer: NumericBuffer; ptr: number; stride: number }> = []
      const release = () => { for (const { ptr } of buffers) module._free(ptr) }
      const finish = (code: number) => {
        backend.lastError = code === 0 ? '' : module.UTF8ToString(module._encos_get_last_error_message?.() ?? 0)
        for (const { buffer, ptr, stride } of buffers) {
          for (let i = 0; i < buffer.length; i++) buffer.set(i, module.getValue(ptr + i * stride, buffer.type))
        }
        return code
      }
      try {
        const converted = args.map(arg => {
          if (!(arg instanceof NumericBuffer)) return arg
          const stride = arg.type === 'double' ? 8 : arg.type === 'i32' ? 4 : 1
          const ptr = module._malloc(arg.length * stride)
          buffers.push({ buffer: arg, ptr, stride })
          for (let i = 0; i < arg.length; i++) module.setValue(ptr + i * stride, arg.get(i), arg.type)
          return ptr
        })
        if (options?.async) {
          active = true
          return Promise.resolve(fn(...converted)).then(finish).finally(() => {
            active = false
            release()
          })
        }
        const code = finish(fn(...converted))
        release()
        return code
      } catch (error) {
        if (options?.async) active = false
        release()
        throw error
      }
    }
  }
  backend.calls = createRuntimeCalls(wrap)
  return backend
}
