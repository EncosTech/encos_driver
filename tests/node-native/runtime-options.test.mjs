// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { resolve } from 'node:path'
import { pathToFileURL } from 'node:url'
import { test } from 'node:test'

const root = resolve(import.meta.dirname, '../..')
const { createEncosRuntime } = await import(
  pathToFileURL(resolve(root, 'npm/dist/create-runtime-native.js')).href
)

function fakeContext() {
  return {
    call: () => ({ code: 0, outputs: [], message: '' }),
    callAsync: async () => ({ code: 0, outputs: [], message: '' }),
    invoke: () => ({ code: 0, message: '', value: undefined }),
    invokeAsync: async () => ({ code: 0, message: '', value: undefined }),
    subscribe: () => {},
    dispose: () => {},
  }
}

test('packageRoot and loadAddon override addon/plugin paths', async () => {
  let addonPath = ''
  let pluginPath = ''
  const runtime = await createEncosRuntime({
    packageRoot: '/opt/encos driver',
    loadAddon(path) {
      addonPath = path
      return {
        createRuntime(path) {
          pluginPath = path
          return fakeContext()
        },
      }
    },
  })
  assert.equal(addonPath, '/opt/encos driver/native/encosdriver.node')
  assert.equal(pluginPath, '/opt/encos driver/runtime/lib/encosPlugins')
  runtime.dispose()
})

test('default addon path derives from import.meta.url package root', async () => {
  let addonPath = ''
  const runtime = await createEncosRuntime({
    loadAddon(path) {
      addonPath = path
      return { createRuntime: () => fakeContext() }
    },
  })
  assert.equal(addonPath, resolve(root, 'npm/native/encosdriver.node'))
  runtime.dispose()
})
