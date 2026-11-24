// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import { chmodSync, copyFileSync, mkdirSync, readdirSync, realpathSync, statSync } from 'node:fs'
import { join } from 'node:path'

// npm 不收录符号链接，显式跟随真实路径，避免版本化共享库的 SONAME 丢失。
export function copyRuntime(source, destination, ancestors = new Set()) {
  const resolved = realpathSync(source)
  const stat = statSync(resolved)
  if (stat.isDirectory()) {
    if (ancestors.has(resolved)) throw new Error(`Cyclic runtime directory: ${source}`)
    mkdirSync(destination, { recursive: true })
    const parents = new Set([...ancestors, resolved])
    for (const name of readdirSync(resolved)) copyRuntime(join(resolved, name), join(destination, name), parents)
  } else if (stat.isFile()) {
    copyFileSync(resolved, destination)
    chmodSync(destination, stat.mode & 0o777)
  } else {
    throw new Error(`Unsupported runtime entry: ${source}`)
  }
}
