// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { createRequire } from 'node:module'
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs'
import { execFileSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const require = createRequire(join(repository, 'npm/package.json'))
const ts = require('typescript')
const directory = resolve(process.argv[2])
const manifest = JSON.parse(readFileSync(join(directory, 'selector-manifest.json'), 'utf8'))
assert.match(manifest.filename, /^[a-z0-9.-]+\.tgz$/)
const work = mkdtempSync(join(tmpdir(), 'encos selector types '))
try {
  writeFileSync(join(work, 'package.json'), JSON.stringify({ private: true, type: 'module' }))
  execFileSync('npm', ['install', '--ignore-scripts', '--omit=optional', '--offline', '--no-audit', '--no-fund', '--cache', join(work, 'cache'), join(directory, manifest.filename)], { cwd: work, stdio: 'inherit' })
  const source = readFileSync(join(repository, 'tests/node-native/public-api.type-test.ts'), 'utf8').replace('../../npm/src/index-native.js', '@encos/encos-driver-native')
  writeFileSync(join(work, 'consumer.ts'), source)
  const program = ts.createProgram([join(work, 'consumer.ts')], {
    target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.NodeNext, moduleResolution: ts.ModuleResolutionKind.NodeNext,
    strict: true, noEmit: true, types: [], skipLibCheck: false,
  })
  const diagnostics = ts.getPreEmitDiagnostics(program)
  assert.equal(diagnostics.length, 0, ts.formatDiagnosticsWithColorAndContext(diagnostics, {
    getCanonicalFileName: name => name, getCurrentDirectory: () => work, getNewLine: () => '\n',
  }))
  console.log('PASS: standalone selector declarations compile without any platform package')
} finally { rmSync(work, { recursive: true, force: true }) }
