// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import { createHash } from 'node:crypto'
import { cpSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { execFileSync } from 'node:child_process'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { parseArgs } from 'node:util'

const { values } = parseArgs({ options: { dist: { type: 'string' }, output: { type: 'string' } } })
if (!values.dist || !values.output) throw new Error('Usage: pack-selector.mjs --dist <compiled npm/dist> --output <directory>')
const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const source = join(repository, 'npm-native')
const dist = resolve(values.dist)
const output = resolve(values.output)
const version = readFileSync(join(repository, 'CMakeLists.txt'), 'utf8').match(/^project\(encos_driver VERSION ([0-9.]+)\)/m)?.[1]
if (!version) throw new Error('Missing project version')
const metadata = JSON.parse(readFileSync(join(source, 'package.json'), 'utf8'))
metadata.version = version
metadata.optionalDependencies = Object.fromEntries(Object.keys(metadata.optionalDependencies).map(name => [name, version]))
delete metadata.scripts
// 导入公共 JS 入口仅用于枚举运行时导出；原生模块在 createEncosRuntime 调用时才加载。
const api = await import(pathToFileURL(join(dist, 'index-native.js')).href)
const names = Object.keys(api).sort()
if (!names.includes('createEncosRuntime') || names.some(name => !/^[A-Za-z_$][\w$]*$/.test(name) || name === 'default')) {
  throw new Error('Unsupported native export surface; update selector generation')
}
const staging = join(output, 'staging')
mkdirSync(output, { recursive: true })
rmSync(staging, { recursive: true, force: true })
mkdirSync(join(staging, 'dist'), { recursive: true })
mkdirSync(join(staging, 'types'))
cpSync(join(source, 'src/loader.js'), join(staging, 'dist/loader.js'))
const license = '// SPDX-FileCopyrightText: 2026 Encos\n// SPDX-License-Identifier: MIT\n'
writeFileSync(join(staging, 'dist/index.js'), `${license}\nimport { loadBackend } from './loader.js'\nconst backend = await loadBackend({ version: ${JSON.stringify(version)} })\nexport const {\n${names.map(name => `  ${name},`).join('\n')}\n} = backend\n`)
for (const entry of readdirSync(dist)) {
  if (entry.endsWith('.d.ts')) cpSync(join(dist, entry), join(staging, 'types', entry))
}
for (const name of ['README.md', 'API.md']) cpSync(join(source, name), join(staging, name))
cpSync(join(repository, 'LICENSE'), join(staging, 'LICENSE'))
writeFileSync(join(staging, 'package.json'), JSON.stringify(metadata, null, 2) + '\n')
const npmCli = process.env.ENCOS_NPM_CLI ?? (process.platform === 'win32' ? join(dirname(process.execPath), 'node_modules/npm/bin/npm-cli.js') : undefined)
const command = npmCli ? process.execPath : 'npm'
const packed = JSON.parse(execFileSync(command, [...(npmCli ? [npmCli] : []), 'pack', '--ignore-scripts', '--json', '--pack-destination', output], { cwd: staging, encoding: 'utf8' }))
const filename = packed[0].filename
writeFileSync(join(output, 'selector-manifest.json'), JSON.stringify({
  package: metadata.name, version, filename,
  sha256: createHash('sha256').update(readFileSync(join(output, filename))).digest('hex'),
  optionalDependencies: metadata.optionalDependencies,
}, null, 2) + '\n')
console.log(join(output, filename))
