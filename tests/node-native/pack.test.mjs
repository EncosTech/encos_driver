// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { chmodSync, lstatSync, mkdirSync, mkdtempSync, readFileSync, rmSync, symlinkSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import test from 'node:test'
import { copyRuntime } from '../../scripts/node/copy-runtime.mjs'

test('npm archive retains shared-library SONAME aliases as regular files', { skip: process.platform === 'win32' }, () => {
  const work = mkdtempSync(join(tmpdir(), 'encos npm soname '))
  try {
    const source = join(work, 'source')
    const staging = join(work, 'package')
    mkdirSync(source)
    mkdirSync(staging)
    writeFileSync(join(source, 'libdriver.so.4.0.1'), 'shared library fixture')
    chmodSync(join(source, 'libdriver.so.4.0.1'), 0o755)
    symlinkSync('libdriver.so.4.0.1', join(source, 'libdriver.so.4'))
    symlinkSync('libdriver.so.4', join(source, 'libdriver.so'))
    copyRuntime(source, join(staging, 'runtime'))
    writeFileSync(join(staging, 'package.json'), JSON.stringify({ name: 'encos-soname-fixture', version: '1.0.0', files: ['runtime'] }))
    const packed = JSON.parse(execFileSync('npm', ['pack', '--ignore-scripts', '--offline', '--dry-run', '--json'], { cwd: staging, encoding: 'utf8' }))
    for (const name of ['libdriver.so', 'libdriver.so.4', 'libdriver.so.4.0.1']) {
      assert.ok(packed[0].files.some(file => file.path === `runtime/${name}`), `npm omitted ${name}`)
      const path = join(staging, 'runtime', name)
      assert.equal(lstatSync(path).isSymbolicLink(), false)
      assert.equal(lstatSync(path).mode & 0o777, 0o755)
      assert.equal(readFileSync(path, 'utf8'), 'shared library fixture')
    }
  } finally {
    rmSync(work, { recursive: true, force: true })
  }
})
