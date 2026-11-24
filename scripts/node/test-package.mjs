// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import { createHash } from 'node:crypto'
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { execFileSync } from 'node:child_process'

const directory = resolve(process.argv[2] ?? '')
const manifest = JSON.parse(readFileSync(join(directory, 'node-manifest.json'), 'utf8'))
if (manifest.platform !== process.platform || manifest.arch !== process.arch) throw new Error('Wrong candidate platform')
if (!/^[a-z0-9.-]+\.tgz$/.test(manifest.filename)) throw new Error('Invalid archive filename')
const archive = join(directory, manifest.filename)
if (createHash('sha256').update(readFileSync(archive)).digest('hex') !== manifest.sha256) throw new Error('Candidate checksum mismatch')
const work = mkdtempSync(join(tmpdir(), 'encos node consumer '))
const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const npmCli = process.env.ENCOS_NPM_CLI ?? (process.platform === 'win32'
  ? join(dirname(process.execPath), 'node_modules/npm/bin/npm-cli.js') : undefined)
try {
  writeFileSync(join(work, 'package.json'), JSON.stringify({ private: true, type: 'module' }))
  execFileSync(npmCli ? process.execPath : 'npm', [
    ...(npmCli ? [npmCli] : []), 'install', '--ignore-scripts', '--no-audit', '--no-fund', '--offline', archive,
  ], { cwd: work, stdio: 'inherit' })
  const packageRoot = join(work, 'node_modules', manifest.package)
  const installed = JSON.parse(readFileSync(join(packageRoot, 'package.json'), 'utf8'))
  if (installed.name !== manifest.package || installed.version !== manifest.version) throw new Error('Installed identity mismatch')
  execFileSync(process.execPath, ['--input-type=module', '-e',
    `const m=await import(${JSON.stringify(manifest.package)});const r=await m.createEncosRuntime();r.dispose();console.log('Package entry loaded')`],
  { cwd: work, stdio: 'inherit' })
  execFileSync(process.execPath, ['--test', '--test-timeout=30000', join(repository, 'tests/node-native/highlevel.test.mjs'),
    join(repository, 'tests/node-native/abi.test.mjs'),
    join(repository, 'tests/node-native/structured.test.mjs'),
    join(repository, 'tests/node-native/full-devices.test.mjs'),
    join(repository, 'tests/node-native/full-highlevel.test.mjs'),
    join(repository, 'tests/node-native/utilities.test.mjs')], {
    cwd: work, stdio: 'inherit', env: { ...process.env, ENCOS_NODE_PACKAGE_ROOT: packageRoot },
  })
  console.log(`PASS: ${manifest.package} ${manifest.version} on ${process.version}`)
} finally {
  rmSync(work, { recursive: true, force: true })
}
