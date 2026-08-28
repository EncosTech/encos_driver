import { copyFileSync, existsSync, mkdirSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const buildDir = resolve(root, '..', 'build-wasm')
const distDir = resolve(root, 'dist')
const artifacts = ['encosdriver_wasm.js', 'encosdriver_wasm.wasm']

mkdirSync(distDir, { recursive: true })

for (const name of artifacts) {
  const source = resolve(buildDir, name)
  const target = resolve(distDir, name)
  if (!existsSync(source)) {
    throw new Error(`missing wasm build artifact: ${source}`)
  }
  mkdirSync(dirname(target), { recursive: true })
  copyFileSync(source, target)
}
