import type { WasmModule, WasmModuleFactory } from './types.js'
import type { RuntimeHost } from './host.js'

const VITE_NODE_MODULES_MODULE_PATH =
  '/node_modules/@encos/encos-driver/dist/encosdriver_wasm.js'
const LEGACY_VITE_NODE_MODULES_MODULE_PATH =
  '/node_modules/@encos/motor-driver/dist/EncosMotorDriverWasm.js'

function defaultModuleUrl(): string {
  return new URL('./encosdriver_wasm.js', import.meta.url).href
}

function legacyModuleUrl(): string {
  return new URL('./EncosMotorDriverWasm.js', import.meta.url).href
}

function moduleUrls(modulePath?: string): string[] {
  if (modulePath !== undefined) {
    return [new URL(modulePath, import.meta.url).href]
  }
  const urls = [defaultModuleUrl(), legacyModuleUrl()]
  if (typeof globalThis.location !== 'undefined') {
    urls.push(new URL(VITE_NODE_MODULES_MODULE_PATH, globalThis.location.href).href)
    urls.push(new URL(LEGACY_VITE_NODE_MODULES_MODULE_PATH, globalThis.location.href).href)
  }
  return urls
}

async function importWasmFactory(moduleUrl: string): Promise<WasmModuleFactory> {
  const loaded = await import(/* @vite-ignore */ moduleUrl)
  const factory = loaded.default ?? loaded
  if (typeof factory !== 'function') {
    throw new Error('WASM module factory is not callable')
  }
  return factory as WasmModuleFactory
}

async function loadFromModuleUrl(moduleUrl: string): Promise<WasmModule> {
  const createModule = await importWasmFactory(moduleUrl)
  return await createModule({
    locateFile(path: string): string {
      return new URL(path, moduleUrl).href
    },
  })
}

export function createBrowserHost(): RuntimeHost {
  return {
    async loadWasmModule(modulePath?: string): Promise<WasmModule> {
      let lastError: unknown
      for (const moduleUrl of moduleUrls(modulePath)) {
        try {
          return await loadFromModuleUrl(moduleUrl)
        } catch (error) {
          lastError = error
        }
      }
      throw lastError
    },

    resolveUrl(input: string): URL {
      return new URL(input, globalThis.location?.href ?? import.meta.url)
    },

    async fetch(input: string): Promise<Response> {
      if (typeof globalThis.fetch !== 'function') {
        throw new Error('fetch is not available in this browser runtime')
      }
      return await globalThis.fetch(input)
    },
  }
}
