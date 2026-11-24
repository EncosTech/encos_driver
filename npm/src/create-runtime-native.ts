// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import { createRequire } from 'node:module'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { createNativeBackend, type NativeContext } from './backend-native.js'
import { Runtime } from './runtime.js'

/** 平台包 native/encosdriver.node 的导出面。 */
export interface NativeAddon {
  createRuntime(pluginPath: string): NativeContext
}

export interface NativeRuntimeOptions {
  /** 覆盖插件目录；默认使用当前平台 npm 包附带的插件。 */
  pluginPath?: string
  /**
   * 覆盖由 import.meta.url 推导的包根目录。
   *
   * 打包（esbuild/rollup）或 Node.js 单可执行文件（SEA）中，import.meta.url
   * 指向 bundle 而非包目录，必须由调用方传入解压/安装后的平台包根目录。
   */
  packageRoot?: string
  /**
   * 覆盖原生 addon 加载方式；默认使用 require()。
   *
   * SEA 中不能可靠地用 require() 加载外部 .node，调用方应传入基于
   * process.dlopen 的实现。参数是解析后的 native/encosdriver.node 绝对路径。
   */
  loadAddon?: (addonPath: string) => NativeAddon
}

function defaultLoadAddon(addonPath: string): NativeAddon {
  return createRequire(import.meta.url)(addonPath) as NativeAddon
}

export async function createEncosRuntime(options: NativeRuntimeOptions = {}): Promise<Runtime> {
  const packageRoot = options.packageRoot ?? resolve(dirname(fileURLToPath(import.meta.url)), '..')
  const addonPath = resolve(packageRoot, 'native/encosdriver.node')
  const addon = (options.loadAddon ?? defaultLoadAddon)(addonPath)
  const context = addon.createRuntime(options.pluginPath ?? resolve(packageRoot, 'runtime/lib/encosPlugins'))
  return new Runtime(createNativeBackend(context), {
    resolveUrl: input => new URL(input),
    fetch: input => globalThis.fetch(input),
  })
}
