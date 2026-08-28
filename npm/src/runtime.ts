import { LogLevel } from './constants.js'
import { allocU32, assertOk, createRuntimeCalls, getU32 } from './native.js'
import type { RuntimeHost } from './host.js'
import type {
  CFunction,
  CreateAdapterOptions,
  CreateFakeAdapterOptions,
  WasmModule,
} from './types.js'
import { AdapterWrapper, FakeAdapterWrapper } from './adapter.js'

let nextDefaultFakeAdapterId = 1

export class Runtime {
  module: WasmModule
  adapters: Set<AdapterWrapper>
  calls: Record<string, CFunction>
  host: RuntimeHost

  constructor(module: WasmModule, host: RuntimeHost) {
    this.module = module
    this.adapters = new Set()
    this.calls = createRuntimeCalls(module)
    this.host = host
  }

  createAdapter<T extends string>(
    options: CreateAdapterOptions<T>,
  ): T extends 'RelayWs' ? Promise<AdapterWrapper> : AdapterWrapper {
    if (options.type === 'RelayWs') {
      return this.#createRelayWsAdapter(options) as T extends 'RelayWs'
        ? Promise<AdapterWrapper>
        : AdapterWrapper
    }

    const handle = this.#createAdapterHandle(options)
    const adapter = new AdapterWrapper(
      this,
      handle,
      options.type,
      options.interfaceName ?? '',
    )
    this.adapters.add(adapter)
    return adapter as T extends 'RelayWs' ? Promise<AdapterWrapper> : AdapterWrapper
  }

  createFakeAdapter(options: CreateFakeAdapterOptions = {}): FakeAdapterWrapper {
    const interfaceName =
      options.interfaceName || `encos-wasm-fake-${nextDefaultFakeAdapterId++}`
    const out = allocU32(this.module)
    try {
      const code = this.calls.createFakeAdapter(
        interfaceName,
        options.loggerName ?? '',
        options.logLevel ?? LogLevel.Info,
        out,
      )
      assertOk(this, code, 'failed to create fake adapter')
      const adapter = new FakeAdapterWrapper(
        this,
        getU32(this.module, out),
        interfaceName,
      )
      this.adapters.add(adapter)
      return adapter
    } finally {
      this.module._free(out)
    }
  }

  dispose(): void {
    for (const adapter of Array.from(this.adapters)) {
      if (!adapter.disposed) {
        adapter.dispose()
      }
    }
    this.adapters.clear()
  }

  #createAdapterHandle(options: CreateAdapterOptions): number {
    const out = allocU32(this.module)
    try {
      const code = this.calls.createAdapter(
        options.type,
        options.interfaceName ?? '',
        options.loggerName ?? '',
        options.logLevel ?? LogLevel.Info,
        out,
      )
      assertOk(this, code, 'failed to create adapter')
      return getU32(this.module, out)
    } finally {
      this.module._free(out)
    }
  }

  async #createRelayWsAdapter(options: CreateAdapterOptions): Promise<AdapterWrapper> {
    const interfaceName = await this.#resolveRelayWsInterfaceName(
      options.interfaceName ?? '',
    )
    const handle = this.#createAdapterHandle({ ...options, interfaceName })
    const adapter = new AdapterWrapper(
      this,
      handle,
      options.type,
      interfaceName,
    )
    this.adapters.add(adapter)
    return adapter
  }

  async #resolveRelayWsInterfaceName(interfaceName: string): Promise<string> {
    if (
      interfaceName.startsWith('ws://') ||
      interfaceName.startsWith('wss://')
    ) {
      return interfaceName
    }

    const startUrl = this.host.resolveUrl(interfaceName)
    const response = await this.host.fetch(startUrl.toString())
    if (!response.ok) {
      throw new Error(
        `RelayWs /start failed: ${response.status} ${response.statusText}`,
      )
    }

    const body = (await response.json()) as { session?: string }
    if (!body.session || typeof body.session !== 'string') {
      throw new Error('RelayWs /start response missing session')
    }

    const wsUrl = new URL('/ws', startUrl)
    wsUrl.protocol = startUrl.protocol === 'https:' ? 'wss:' : 'ws:'
    wsUrl.searchParams.set('session', body.session)
    wsUrl.searchParams.set('freq', '100')
    return wsUrl.toString()
  }
}
