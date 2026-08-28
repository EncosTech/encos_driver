import { existsSync, readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

import type { WasmModule, WasmModuleFactory } from './types.js'
import type { RuntimeHost } from './host.js'

const here = dirname(fileURLToPath(import.meta.url))

function defaultModulePath(): string {
  const candidates = [
    resolve(here, './encosdriver_wasm.js'),
    resolve(here, './EncosMotorDriverWasm.js'),
    resolve(here, '../../build-wasm/encosdriver_wasm.js'),
    resolve(here, '../../build-wasm/EncosMotorDriverWasm.js'),
  ]
  for (const candidate of candidates) {
    if (existsSync(candidate)) {
      return candidate
    }
  }
  return candidates[2]
}

function toImportUrl(modulePath: string): string {
  if (/^[a-zA-Z][a-zA-Z\d+.-]*:/.test(modulePath)) {
    return modulePath
  }
  return pathToFileURL(modulePath).href
}

function toFilePath(modulePath: string): string | null {
  if (modulePath.startsWith('file://')) {
    return fileURLToPath(modulePath)
  }
  if (/^[a-zA-Z][a-zA-Z\d+.-]*:/.test(modulePath)) {
    return null
  }
  return modulePath
}

function wasmPathFor(modulePath: string): string | null {
  const filePath = toFilePath(modulePath)
  if (!filePath) {
    return null
  }
  return filePath.replace(/\.js$/u, '.wasm')
}

async function importWasmFactory(modulePath: string): Promise<WasmModuleFactory> {
  const loaded = await import(toImportUrl(modulePath))
  const factory = loaded.default ?? loaded
  if (typeof factory !== 'function') {
    throw new Error('WASM module factory is not callable')
  }
  return factory as WasmModuleFactory
}

async function ensureNodeWebSocket(): Promise<void> {
  if (typeof globalThis.WebSocket === 'function') {
    return
  }

  const loaded = await import('ws')
  const webSocketCtor = loaded.WebSocket ?? loaded.default
  if (typeof webSocketCtor !== 'function') {
    throw new Error('ws package does not export a WebSocket constructor')
  }

  globalThis.WebSocket = webSocketCtor as typeof globalThis.WebSocket
}

export function createNodeHost(): RuntimeHost {
  return {
    async loadWasmModule(modulePath?: string): Promise<WasmModule> {
      await ensureNodeWebSocket()

      const resolvedModulePath = modulePath ?? defaultModulePath()
      const createModule = await importWasmFactory(resolvedModulePath)
      const wasmPath = wasmPathFor(resolvedModulePath)
      const wasmBinary =
        wasmPath && existsSync(wasmPath) ? readFileSync(wasmPath) : undefined
      return await createModule({
        locateFile(path: string): string {
          if (path.endsWith('.wasm') && wasmPath) {
            return pathToFileURL(wasmPath).href
          }
          return path
        },
        wasmBinary,
      })
    },

    resolveUrl(input: string): URL {
      return new URL(input)
    },

    async fetch(input: string): Promise<Response> {
      if (typeof globalThis.fetch !== 'function') {
        throw new Error('fetch is not available in this Node runtime')
      }
      return await globalThis.fetch(input)
    },
  }
}
