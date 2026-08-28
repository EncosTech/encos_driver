import type { WasmModule } from './types.js'

export interface RuntimeHost {
  loadWasmModule(modulePath?: string): Promise<WasmModule>
  resolveUrl(input: string): URL
  fetch(input: string): Promise<Response>
}

function isNodeRuntime(): boolean {
  return (
    typeof globalThis.process === 'object' &&
    globalThis.process !== null &&
    typeof globalThis.process.versions === 'object' &&
    typeof globalThis.process.versions.node === 'string'
  )
}

export async function createDefaultHost(): Promise<RuntimeHost> {
  if (isNodeRuntime()) {
    const { createNodeHost } = await import('./host-node.js')
    return createNodeHost()
  }

  const { createBrowserHost } = await import('./host-browser.js')
  return createBrowserHost()
}
