// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import { Logger, LogWriter } from './logging.js'
import { DriverManager } from './driver-manager.js'
import { LogLevel } from './constants.js'
import { allocU32, assertOk, getU32 } from './native.js'
import type { RuntimeBackend } from './backend.js'
import type { RuntimeHost } from './host.js'
import type {
  CFunction,
  CreateAdapterOptions,
  CreateFakeAdapterOptions,
  WasmModule,
  WebSerialBridge,
} from './types.js'
import { AdapterWrapper, FakeAdapterWrapper } from './adapter.js'

let nextDefaultFakeAdapterId = 1

export class Runtime {
  readonly manager: DriverManager
  backend: RuntimeBackend
  disposed = false

  get module(): WasmModule {
    if (!this.backend.module) throw new Error('WASM module is unavailable in the native runtime')
    return this.backend.module
  }
  adapters: Set<AdapterWrapper>
  calls: Record<string, CFunction>
  host: Pick<RuntimeHost, 'resolveUrl' | 'fetch'>

  constructor(backend: RuntimeBackend, host: Pick<RuntimeHost, 'resolveUrl' | 'fetch'>) {
    this.backend = backend
    this.manager = new DriverManager(this)
    this.adapters = new Set()
    this.calls = backend.calls
    this.host = host
  }

  get webSerial(): WebSerialBridge {
    if (!this.module.webSerial) {
      throw new Error('Web Serial bridge is unavailable in this WASM build')
    }
    return this.module.webSerial
  }

  createAdapter<T extends string>(
    options: CreateAdapterOptions<T>,
  ): T extends 'RelayWs' ? Promise<AdapterWrapper> : AdapterWrapper {
    this.#assertLive()
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
    this.#assertLive()
    const interfaceName =
      options.interfaceName || (this.backend.kind === 'native'
        ? `encos-native-fake-${globalThis.crypto.randomUUID()}`
        : `encos-wasm-fake-${nextDefaultFakeAdapterId++}`)
    const out = allocU32()
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
        getU32(out),
        interfaceName,
      )
      this.adapters.add(adapter)
      return adapter
    } finally {
      out.release()
    }
  }

  dispose(): void {
    if (this.disposed) return
    for (const adapter of Array.from(this.adapters)) {
      if (!adapter.disposed) {
        adapter.dispose()
      }
    }
    this.adapters.clear()
    this.backend.dispose?.()
    this.disposed = this.backend.kind === 'native'
  }

  /** @internal 原生结构化调用入口，WASM 不支持时明确报错。 */
  _invoke<T>(name: string, args: Record<string, unknown> = {}): T {
    this.#assertLive()
    if (!this.backend.invoke) throw new Error(`${name} is only supported by the native runtime`)
    return this.backend.invoke<T>(name, args)
  }

  /** @internal 原生异步调用入口。 */
  async _invokeAsync<T>(name: string, args: Record<string, unknown> = {}): Promise<T> {
    this.#assertLive()
    if (!this.backend.invokeAsync) throw new Error(`${name} is only supported by the native runtime`)
    return this.backend.invokeAsync<T>(name, args)
  }

  /** @internal 原生回调注册入口。 */
  _subscribe(name: string, args: Record<string, unknown>, callback: ((value: any) => void) | null): void {
    this.#assertLive()
    if (!this.backend.subscribe) throw new Error(`${name} is only supported by the native runtime`)
    this.backend.subscribe(name, args, callback)
  }

  createLogger(name: string, level: number = LogLevel.Info): Logger {
    return new Logger(this, this._invoke('utility.createLogger', { name, level }))
  }
  createLogWriter(baseName: string, headers: string[]): LogWriter {
    return new LogWriter(this, this._invoke('utility.createLogWriter', { baseName, headers }))
  }
  getVersion(): string { return this._invoke('runtime.getVersion') }
  getAvailableAdapterTypes(): string[] { return this._invoke('runtime.getAvailableAdapterTypes') }
  getAvailableInterface(type: string): string[] { return this._invoke('runtime.getAvailableInterface', { type }) }
  clearLogger(): void { this._invoke('runtime.clearLogger') }
  clearLoggers(): void { this.clearLogger() }
  setCurrentThreadPriority(priority: number): boolean { return this._invoke('runtime.setCurrentThreadPriority', { priority }) }
  deleteAdapterByInterfaceName(interfaceName: string): boolean { return this.manager.destroyAdapterByInterfaceName(interfaceName) }
  unloadAdapterByInterfaceName(interfaceName: string): boolean { return this.manager.destroyAdapterByInterfaceName(interfaceName) }
  deleteAdapter(adapter: AdapterWrapper): boolean { return this.manager.destroyAdapter(adapter) }
  deleteBus(bus: import('./bus.js').BusWrapper): boolean { return this.manager.destroyBus(bus) }
  deleteMotor(motor: import('./motor.js').MotorWrapper): boolean { return this.manager.destroyMotor(motor) }
  deleteBattery(battery: import('./battery.js').BatteryWrapper): boolean { return this.manager.destroyBattery(battery) }
  deleteImu(imu: import('./imu.js').ImuWrapper): boolean { return this.manager.destroyImu(imu) }
  deletePms(pms: import('./pms.js').PmsWrapper): boolean { return this.manager.destroyPms(pms) }
  deleteDexHand(hand: import('./dex-hand.js').DexHandWrapper): boolean { return this.manager.destroyDexHand(hand) }
  deleteGlove(glove: import('./glove.js').GloveWrapper): boolean { return this.manager.destroyGlove(glove) }
  setPluginPath(path: string): void { this._invoke('runtime.setPluginPath', { path }) }
  getMotorModelRanges(model: number): import('./native-types.js').MotorProperties { return this._invoke('runtime.getMotorModelRanges', { model }) }
  stringToMotorModel(name: string): number { return this._invoke('runtime.stringToMotorModel', { name }) }
  motorModelToString(model: number): string { return this._invoke('runtime.motorModelToString', { model }) }
  getAllMotorModelStrings(): string[] { return this._invoke('runtime.getAllMotorModelStrings') }

  #assertLive(): void {
    if (this.disposed) throw new Error('runtime is disposed')
  }

  #createAdapterHandle(options: CreateAdapterOptions): number {
    this.#assertLive()
    if (this.backend.kind === 'native') return this._invoke('runtime.createAdapter', {
      ...options, interfaceName: options.interfaceName ?? '', loggerName: options.loggerName ?? '', logLevel: options.logLevel ?? LogLevel.Info,
    })
    if (options.extend !== undefined) throw new Error('adapter extension configuration is only supported by the native runtime')
    const out = allocU32()
    try {
      const code = this.calls.createAdapter(
        options.type,
        options.interfaceName ?? '',
        options.loggerName ?? '',
        options.logLevel ?? LogLevel.Info,
        out,
      )
      assertOk(this, code, 'failed to create adapter')
      return getU32(out)
    } finally {
      out.release()
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
