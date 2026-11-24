// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import { cpSync, existsSync, mkdirSync, readFileSync, readdirSync, renameSync, rmSync, writeFileSync } from 'node:fs'
import { createHash } from 'node:crypto'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { execFileSync } from 'node:child_process'
import { parseArgs } from 'node:util'
import { copyRuntime } from './copy-runtime.mjs'

const { values } = parseArgs({ options: Object.fromEntries(
  ['dist', 'runtime', 'addon', 'output', 'platform', 'arch'].map(key => [key, { type: 'string' }]),
) })
for (const key of ['dist', 'runtime', 'addon', 'output', 'platform', 'arch']) {
  if (!values[key]) throw new Error(`Missing --${key}`)
}
const suffix = { 'linux-x64': 'linux-x64-gnu', 'linux-arm64': 'linux-arm64-gnu', 'win32-x64': 'win32-x64' }[`${values.platform}-${values.arch}`]
if (!suffix) throw new Error('Unsupported native platform')
const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const version = readFileSync(join(repository, 'CMakeLists.txt'), 'utf8').match(/^project\(encos_driver VERSION ([0-9.]+)\)/m)?.[1]
if (!version) throw new Error('Missing project version')
const output = resolve(values.output)
const staging = join(output, 'staging')
mkdirSync(output, { recursive: true })
rmSync(staging, { recursive: true, force: true })
mkdirSync(join(staging, 'native'), { recursive: true })
cpSync(resolve(values.dist), join(staging, 'dist'), { recursive: true })
for (const name of readdirSync(join(staging, 'dist'))) {
  if (name.endsWith('.wasm') || name.startsWith('encosdriver_wasm') ||
      ['index.js', 'create-runtime.js', 'backend-wasm.js', 'host.js', 'host-node.js', 'host-browser.js'].includes(name)) {
    rmSync(join(staging, 'dist', name))
  }
}
if (!existsSync(join(staging, 'dist/index-native.js'))) throw new Error('Missing compiled native JavaScript entry')
copyRuntime(resolve(values.runtime), join(staging, 'runtime'))
// npm 后端不需要 C++ SDK 开发头文件与 CMake 配置。
rmSync(join(staging, 'runtime/include'), { recursive: true, force: true })
rmSync(join(staging, 'runtime/lib/cmake'), { recursive: true, force: true })
rmSync(join(staging, 'runtime/share/doc/libencosdriver/PAYLOAD.sha256'), { force: true })
if (values.platform === 'win32' && existsSync(join(staging, 'runtime/lib'))) {
  for (const name of readdirSync(join(staging, 'runtime/lib'))) {
    if (name.endsWith('.a') || name.endsWith('.lib')) rmSync(join(staging, 'runtime/lib', name))
  }
}
cpSync(resolve(values.addon), join(staging, 'native/encosdriver.node'))
if (values.platform === 'win32' && existsSync(join(staging, 'runtime/bin'))) {
  for (const name of readdirSync(join(staging, 'runtime/bin'))) {
    if (name.toLowerCase().endsWith('.dll')) renameSync(join(staging, 'runtime/bin', name), join(staging, 'native', name))
  }
}
cpSync(join(repository, 'LICENSE'), join(staging, 'LICENSE'))
cpSync(join(repository, 'COPYING'), join(staging, 'COPYING'))
if (values.platform === 'linux') {
  mkdirSync(join(staging, 'scripts'), { recursive: true })
  cpSync(join(repository, 'scripts/node/authorize.sh'), join(staging, 'scripts/authorize.sh'))
  const audit = execFileSync('python3', [join(repository, 'scripts/portable/bundle.py'), staging,
    '--core-directory', 'runtime/lib', '--private-directory', 'runtime/lib/encosPlugins/deps'], { encoding: 'utf8' })
  writeFileSync(join(output, 'elf-audit.json'), audit)
}
const manifest = {
  name: `@encos/encos-driver-native-${suffix}`, version, type: 'module',
  main: './dist/index-native.js', types: './dist/index-native.d.ts',
  exports: { '.': { types: './dist/index-native.d.ts', import: './dist/index-native.js' } },
  os: [values.platform], cpu: [values.arch], ...(values.platform === 'linux' ? { libc: ['glibc'] } : {}),
  engines: { node: '>=22' }, files: ['dist', 'native', 'runtime', 'scripts', 'LICENSE', 'COPYING'],
  license: values.platform === 'linux'
    ? 'MIT AND GPL-3.0-or-later AND GPL-2.0-only AND LGPL-2.1-only AND LGPL-3.0-or-later AND BSD-2-Clause AND BSD-3-Clause AND Zlib'
    : 'MIT AND GPL-3.0-or-later',
  publishConfig: { registry: 'https://git.in.encos.cn/api/packages/packages/npm/' },
}
writeFileSync(join(staging, 'package.json'), JSON.stringify(manifest, null, 2) + '\n')
for (const name of readdirSync(output)) if (name.endsWith('.tgz')) rmSync(join(output, name))
const npmCli = process.env.ENCOS_NPM_CLI ?? (process.platform === 'win32'
  ? join(dirname(process.execPath), 'node_modules/npm/bin/npm-cli.js') : undefined)
const command = npmCli ? process.execPath : (process.platform === 'win32' ? 'npm.cmd' : 'npm')
const args = [...(npmCli ? [npmCli] : []), 'pack', '--ignore-scripts', '--json', '--pack-destination', output]
const packed = JSON.parse(execFileSync(command, args, { cwd: staging, encoding: 'utf8', shell: process.platform === 'win32' && !npmCli }))
if (values.platform === 'linux') {
  const entries = new Set(packed[0].files.map(file => file.path))
  for (const name of ['libencosdriver', 'libencosdriver_base']) {
    const soname = `runtime/lib/${name}.so.${version.split('.')[0]}`
    if (!entries.has(soname)) throw new Error(`npm archive omitted required SONAME: ${soname}`)
  }
}
const filename = packed[0].filename
writeFileSync(join(output, 'node-manifest.json'), JSON.stringify({
  package: manifest.name, version, platform: values.platform, arch: values.arch, napi: 8,
  filename, sha256: createHash('sha256').update(readFileSync(join(output, filename))).digest('hex'),
}, null, 2) + '\n')
console.log(join(output, filename))
