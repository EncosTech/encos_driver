// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import { NumericBuffer, type RuntimeBackend } from './backend.js'
import { createRuntimeCalls } from './native.js'

export interface NativeCallResult {
  code: number
  outputs: number[][]
  message: string
}

export interface NativeContext {
  call(name: string, args: unknown[]): NativeCallResult
  callAsync(name: string, args: unknown[]): Promise<NativeCallResult>
  invoke(name: string, args: Record<string, unknown>): { code: number; message: string; value: unknown }
  invokeAsync(name: string, args: Record<string, unknown>): Promise<{ code: number; message: string; value: unknown }>
  subscribe(name: string, args: Record<string, unknown>, callback: ((value: any) => void) | null): void
  dispose(): void
}

export function createNativeBackend(context: NativeContext): RuntimeBackend {
  const unwrap = <T>(result: { code: number; message: string; value: unknown }): T => {
    if (result.code !== 0) {
      const error = new Error(result.message) as Error & { code: number }
      error.name = 'EncosNativeError'
      error.code = result.code
      throw error
    }
    return result.value as T
  }
  const backend: RuntimeBackend = {
    kind: 'native', calls: {}, lastError: '', dispose: () => context.dispose(),
    invoke: <T>(name: string, args: Record<string, unknown>) => unwrap<T>(context.invoke(name, args)),
    invokeAsync: <T>(name: string, args: Record<string, unknown>) => context.invokeAsync(name, args).then(result => unwrap<T>(result)),
    subscribe: (name, args, callback) => context.subscribe(name, args, callback),
  }
  backend.calls = createRuntimeCalls((name, _returnType, _argTypes, options) => (...args: unknown[]) => {
    const inputs: unknown[] = []
    const outputs: NumericBuffer[] = []
    const inputBuffers = name === 'encos_fake_inject_feedback' || name === 'encos_fake_inject_raw_message'
    for (let index = 0; index < args.length; index++) {
      const arg = args[index]
      if (arg instanceof NumericBuffer) {
        if (inputBuffers) inputs.push(arg.values)
        else outputs.push(arg)
        if (arg.span) index++
      } else inputs.push(arg)
    }
    const finish = (result: NativeCallResult): number => {
      if (result.code !== 0) {
        const error = new Error(result.message || `native call failed: ${name}`) as Error & { code: number }
        error.name = 'EncosNativeError'
        error.code = result.code
        throw error
      }
      for (let index = 0; index < outputs.length; index++) {
        outputs[index].values = result.outputs[index] ?? []
      }
      return result.code
    }
    return options?.async
      ? context.callAsync(name, inputs).then(finish)
      : finish(context.call(name, inputs))
  })
  return backend
}
