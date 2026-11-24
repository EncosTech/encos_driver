// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { test } from 'node:test'
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { pathToFileURL } from 'node:url'
import { selectPlatform, loadBackend } from '../../npm-native/src/loader.js'

const version = '1.2.3'
function fixture(t, metadata = {}) {
  const root = mkdtempSync(join(tmpdir(), 'encos selector fixture '))
  t.after(() => rmSync(root, { recursive: true, force: true }))
  mkdirSync(join(root, 'dist'))
  writeFileSync(join(root, 'package.json'), JSON.stringify({
    type: 'module', name: '@encos/encos-driver-native-linux-x64-gnu', version, ...metadata,
  }))
  writeFileSync(join(root, 'dist/index-native.js'), 'export const marker = 42; export function createEncosRuntime() { return Promise.resolve("fixture") }\n')
  return pathToFileURL(join(root, 'dist/index-native.js')).href
}

test('selects exact supported platform without WASM fallback', () => {
  assert.equal(selectPlatform('linux', 'x64', '2.28'), '@encos/encos-driver-native-linux-x64-gnu')
  assert.equal(selectPlatform('linux', 'arm64', '2.39'), '@encos/encos-driver-native-linux-arm64-gnu')
  assert.equal(selectPlatform('win32', 'x64'), '@encos/encos-driver-native-win32-x64')
  for (const [platform, arch, libc] of [['darwin', 'arm64'], ['win32', 'arm64'], ['linux', 'riscv64', '2.39']]) {
    assert.throws(() => selectPlatform(platform, arch, libc), /unsupported/i)
  }
  assert.throws(() => selectPlatform('linux', 'x64', undefined), /musl|glibc/i)
  assert.throws(() => selectPlatform('linux', 'x64', '2.27'), /2\.28/)
})

test('loads the selected real ES module and preserves its exports', async t => {
  const entry = fixture(t)
  const backend = await loadBackend({ version, platform: 'linux', arch: 'x64', glibcVersion: '2.36', resolve: name => {
    assert.equal(name, '@encos/encos-driver-native-linux-x64-gnu')
    return entry
  } })
  assert.equal(backend.marker, 42)
  assert.equal(await backend.createEncosRuntime(), 'fixture')
})

test('rejects backend version/identity mismatch before importing its code', async t => {
  for (const metadata of [{ version: '1.2.4' }, { name: '@encos/other' }]) {
    const entry = fixture(t, metadata)
    await assert.rejects(loadBackend({ version, platform: 'linux', arch: 'x64', glibcVersion: '2.36', resolve: () => entry }), /mismatch/i)
  }
})

test('missing optional dependency produces actionable error and retains its cause', async () => {
  const cause = Object.assign(new Error('missing'), { code: 'ERR_MODULE_NOT_FOUND' })
  await assert.rejects(loadBackend({ version, platform: 'win32', arch: 'x64', resolve: () => { throw cause } }), error => {
    assert.match(error.message, /@encos\/encos-driver-native-win32-x64/)
    assert.match(error.message, /include=optional/)
    assert.equal(error.cause, cause)
    return true
  })
})

test('native initialization errors remain distinguishable from missing optional package', async t => {
  const entry = fixture(t)
  writeFileSync(new URL(entry), 'throw new Error("native initialization failed")\n')
  await assert.rejects(loadBackend({ version, platform: 'linux', arch: 'x64', glibcVersion: '2.36', resolve: () => entry }), /native initialization failed/)
})
