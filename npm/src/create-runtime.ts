import { createDefaultHost } from './host.js'
import type { RuntimeOptions } from './types.js'
import { Runtime } from './runtime.js'

export async function createEncosRuntime(options: RuntimeOptions = {}): Promise<Runtime> {
  const host = await createDefaultHost()
  const module = await host.loadWasmModule(options.modulePath)
  return new Runtime(module, host)
}
