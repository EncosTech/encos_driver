// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import { readFileSync } from 'node:fs'

export function selectPlatform(platform, arch, glibcVersion) {
  if (platform === 'win32' && arch === 'x64') return '@encos/encos-driver-native-win32-x64'
  if (platform === 'linux' && (arch === 'x64' || arch === 'arm64')) {
    if (typeof glibcVersion !== 'string' || !/^\d+\.\d+/.test(glibcVersion)) {
      throw new Error('EncosDriver native requires glibc >= 2.28 on Linux; musl and unknown libc are unsupported.')
    }
    const [major, minor] = glibcVersion.split('.').map(Number)
    if (major < 2 || (major === 2 && minor < 28)) {
      throw new Error(`EncosDriver native requires glibc >= 2.28; found ${glibcVersion}.`)
    }
    return `@encos/encos-driver-native-linux-${arch}-gnu`
  }
  throw new Error(`Unsupported EncosDriver native platform: ${platform}/${arch}. Supported: Linux glibc x64/arm64, Windows x64.`)
}

export async function loadBackend({
  version,
  platform = process.platform,
  arch = process.arch,
  glibcVersion = platform === 'linux' ? process.report.getReport().header.glibcVersionRuntime : undefined,
  resolve = specifier => import.meta.resolve(specifier),
}) {
  const name = selectPlatform(platform, arch, glibcVersion)
  let entry
  try {
    entry = resolve(name)
  } catch (cause) {
    if (cause.code !== 'ERR_MODULE_NOT_FOUND' && cause.code !== 'MODULE_NOT_FOUND') throw cause
    throw new Error(`Missing optional native package ${name}@${version}. Reinstall @encos/encos-driver-native with npm install --include=optional and access to the Encos npm registry.`, { cause })
  }
  // 平台包的公开入口固定为 dist/index-native.js；读取元数据不执行原生代码。
  const metadata = JSON.parse(readFileSync(new URL('../package.json', entry), 'utf8'))
  if (metadata.name !== name || metadata.version !== version) {
    throw new Error(`EncosDriver native package mismatch: expected ${name}@${version}, found ${metadata.name}@${metadata.version}. Reinstall matching packages.`)
  }
  return import(entry)
}
