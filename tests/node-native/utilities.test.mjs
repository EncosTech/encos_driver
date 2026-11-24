// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { test } from 'node:test'
import { mkdtempSync, readFileSync, rmSync, existsSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { pathToFileURL } from 'node:url'
import { spawnSync } from 'node:child_process'
const packageRoot = process.env.ENCOS_NODE_PACKAGE_ROOT
if (!packageRoot) throw new Error('ENCOS_NODE_PACKAGE_ROOT is required')
const { createEncosRuntime } = await import(pathToFileURL(resolve(packageRoot, 'dist/index-native.js')).href)

test('native log writer preserves CSV scalars, validates rows, flushes and closes', async t => {
  const dir = mkdtempSync(join(tmpdir(), 'encos-native-log-'))
  const runtime = await createEncosRuntime()
  t.after(() => { runtime.dispose(); rmSync(dir, { recursive: true, force: true }) })
  const baseName = join(dir, 'values')
  const handle = runtime._invoke('utility.createLogWriter', { baseName, headers: ['a', 'b', 'c'] })
  assert.equal(runtime._invoke('utility.writerBaseName', { handle }), baseName)
  const filename = runtime._invoke('utility.writerFileName', { handle })
  runtime._invoke('utility.writerWrite', { handle, fields: ['a,"b', null, true] })
  runtime._invoke('utility.writerWrite', { handle, fields: [NaN, Infinity, -Infinity] })
  runtime._invoke('utility.writerWrite', { handle, fields: [9007199254740993n, 1.25, -7] })
  assert.throws(() => runtime._invoke('utility.writerWrite', { handle, fields: [1] }))
  assert.throws(() => runtime._invoke('utility.writerWrite', { handle, fields: [{}, 2, 3] }))
  runtime._invoke('utility.writerFlush', { handle })
  assert.ok(existsSync(filename))
  assert.ok(readFileSync(filename).length > 0)
  // Use zstd when installed; Windows CI still verifies a complete nonempty compressed stream.
  const decoded = spawnSync('zstd', ['-dc', filename], { encoding: 'utf8' })
  if (!decoded.error) {
    assert.equal(decoded.status, 0, decoded.stderr)
    assert.equal(decoded.stdout, 'a,b,c\n"a,""b",,true\nnan,inf,-inf\n9007199254740993,1.25,-7\n')
  }
  runtime._invoke('utility.writerDispose', { handle })
  assert.throws(() => runtime._invoke('utility.writerFlush', { handle }))
})

test('native logger exposes name and level with scoped lifetime', async t => {
  const runtime = await createEncosRuntime()
  t.after(() => runtime.dispose())
  const handle = runtime._invoke('utility.createLogger', { name: `node-log-${process.pid}`, level: 6 })
  assert.equal(runtime._invoke('utility.loggerName', { handle }), `node-log-${process.pid}`)
  assert.equal(runtime._invoke('utility.loggerLevel', { handle }), 6)
  runtime._invoke('utility.loggerSetLevel', { handle, level: 5 })
  assert.equal(runtime._invoke('utility.loggerLevel', { handle }), 5)
  for (let level = 0; level < 5; level++) runtime._invoke('utility.loggerLog', { handle, level, message: 'suppressed' })
  assert.throws(() => runtime._invoke('utility.loggerSetLevel', { handle, level: 7 }))
  runtime._invoke('utility.loggerFlush', { handle })
  runtime._invoke('utility.loggerDispose', { handle })
  assert.throws(() => runtime._invoke('utility.loggerLevel', { handle }))
})

test('runtime disposal flushes live log writers', async t => {
  const dir = mkdtempSync(join(tmpdir(), 'encos-native-close-'))
  const runtime = await createEncosRuntime()
  t.after(() => { runtime.dispose(); rmSync(dir, { recursive: true, force: true }) })
  const handle = runtime._invoke('utility.createLogWriter', { baseName: join(dir, 'close'), headers: ['value'] })
  const filename = runtime._invoke('utility.writerFileName', { handle })
  runtime._invoke('utility.writerWrite', { handle, fields: [123] })
  runtime.dispose()
  assert.ok(readFileSync(filename).length > 0)
})
