// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { createServer } from 'node:http'
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { spawn } from 'node:child_process'

function candidate(directory, manifestName) {
  const metadata = JSON.parse(readFileSync(join(directory, manifestName), 'utf8'))
  if (!/^[a-z0-9.-]+\.tgz$/.test(metadata.filename)) throw new Error('Invalid candidate filename')
  const filename = join(directory, metadata.filename)
  const bytes = readFileSync(filename)
  assert.equal(createHash('sha256').update(bytes).digest('hex'), metadata.sha256, 'candidate checksum')
  return { ...metadata, filename, bytes }
}
const platform = candidate(resolve(process.argv[2] ?? ''), 'node-manifest.json')
const selector = candidate(resolve(process.argv[3] ?? ''), 'selector-manifest.json')
assert.equal(platform.platform, process.platform)
assert.equal(platform.arch, process.arch)
assert.equal(selector.package, '@encos/encos-driver-native')
assert.equal(selector.version, platform.version)
assert.equal(selector.optionalDependencies[platform.package], platform.version)
const work = mkdtempSync(join(tmpdir(), 'encos selector consumer '))
const tarballRequests = []
const dependencies = {
  '@encos/encos-driver-native-linux-x64-gnu': { os: ['linux'], cpu: ['x64'], libc: ['glibc'] },
  '@encos/encos-driver-native-linux-arm64-gnu': { os: ['linux'], cpu: ['arm64'], libc: ['glibc'] },
  '@encos/encos-driver-native-win32-x64': { os: ['win32'], cpu: ['x64'] },
}
assert.deepEqual(selector.optionalDependencies, Object.fromEntries(Object.keys(dependencies).map(name => [name, selector.version])))
// 使用仅监听回环地址的临时 registry，安装未经修改的真实归档；不发布候选到外部服务。
let registry
const server = createServer((request, response) => {
  const path = decodeURIComponent(new URL(request.url, 'http://localhost').pathname)
  if (path.startsWith('/tarballs/')) {
    const name = path.slice('/tarballs/'.length)
    tarballRequests.push(name)
    if (name !== platform.package) { response.writeHead(404); response.end(); return }
    response.writeHead(200, { 'content-type': 'application/octet-stream' })
    response.end(platform.bytes)
    return
  }
  const name = path.slice(1)
  if (!Object.hasOwn(dependencies, name)) { response.writeHead(404); response.end(); return }
  const pack = {
    name, version: platform.version, ...dependencies[name],
    dist: {
      tarball: `${registry}tarballs/${name}`,
      ...(name === platform.package ? { integrity: `sha512-${createHash('sha512').update(platform.bytes).digest('base64')}` } : {}),
    },
  }
  response.writeHead(200, { 'content-type': 'application/json' })
  response.end(JSON.stringify({ name, 'dist-tags': { latest: platform.version }, versions: { [platform.version]: pack } }))
})
function run(command, args, options = {}) {
  return new Promise((done, reject) => {
    const child = spawn(command, args, { cwd: work, stdio: 'inherit', ...options })
    child.once('error', reject)
    child.once('exit', (code, signal) => code === 0 ? done() : reject(new Error(`${command} failed: ${signal ?? code}`)))
  })
}
try {
  await new Promise((done, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', done) })
  registry = `http://127.0.0.1:${server.address().port}/`
  writeFileSync(join(work, 'package.json'), JSON.stringify({ private: true, type: 'module' }))
  writeFileSync(join(work, '.npmrc'), `registry=${registry}\n@encos:registry=${registry}\n`)
  const npmCli = process.env.ENCOS_NPM_CLI ?? (process.platform === 'win32' ? join(dirname(process.execPath), 'node_modules/npm/bin/npm-cli.js') : undefined)
  const env = { ...process.env, NPM_CONFIG_USERCONFIG: join(work, '.npmrc'), NPM_CONFIG_CACHE: join(work, 'cache'), NO_PROXY: '127.0.0.1,localhost' }
  await run(npmCli ? process.execPath : 'npm', [...(npmCli ? [npmCli] : []), 'install', '--ignore-scripts', '--include=optional', '--no-audit', '--no-fund', '--registry', registry, selector.filename], { env })
  assert.deepEqual([...new Set(tarballRequests)], [platform.package], 'npm must download only the current platform')
  await run(process.execPath, ['--input-type=module', '-e', `
    import assert from 'node:assert/strict'
    import { existsSync, readFileSync } from 'node:fs'
    import { join } from 'node:path'
    const api = await import(${JSON.stringify(selector.package)})
    const backend = await import(${JSON.stringify(platform.package)})
    assert.deepEqual(Object.keys(api).sort(), Object.keys(backend).sort())
    for (const name of Object.keys(backend)) assert.equal(api[name], backend[name], name)
    for (const name of ${JSON.stringify(Object.keys(dependencies))}) {
      assert.equal(existsSync(join('node_modules', name)), name === ${JSON.stringify(platform.package)})
    }
    const runtime = await api.createEncosRuntime()
    try {
      assert.equal(runtime.getVersion(), ${JSON.stringify(selector.version)})
      const adapter = runtime.createFakeAdapter()
      adapter.fake.seedMotor(0, 1, api.MotorModel.EC_A4310_P2)
      const motor = await adapter.getBus(0).getMotor(1, api.MotorModel.EC_A4310_P2)
      const result = await motor.spdControl({speed: 1, current: 2, feedbackType: 1})
      assert.equal(result.noResponse, false)
      assert.ok(Number.isFinite(await motor.getParameter(api.MotorParameter.Position)))
      assert.ok(adapter.fake.getDecodedCommandRecords().length > 0)
      assert.equal(runtime.getAllMotorModelStrings().length, 72)
      assert.equal(typeof api.checkPermissions, 'function')
      assert.equal(typeof api.requestPermissions, 'function')
      assert.equal(typeof adapter.getGlove, 'function')
      assert.equal(typeof adapter.getBus(0).getPms, 'function')
    } finally { runtime.dispose() }
    console.log('Selector forwards every backend export; Fake commands passed')
  `])
  // 验证省略 optionalDependencies 后的错误路径，不依赖既有 npm 缓存。
  rmSync(join(work, 'node_modules', platform.package), { recursive: true, force: true })
  await run(process.execPath, ['--input-type=module', '-e', `
    import assert from 'node:assert/strict'
    await assert.rejects(import(${JSON.stringify(selector.package)}), error => /include=optional/.test(error.message) && error.message.includes(${JSON.stringify(platform.package)}))
  `])
  console.log(`PASS: ${selector.package} ${selector.version} selects ${platform.package} on ${process.version}`)
} finally {
  server.closeAllConnections()
  await new Promise(done => server.close(done))
  rmSync(work, { recursive: true, force: true })
}
