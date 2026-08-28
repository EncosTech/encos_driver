import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const repoRoot = resolve(root, '..')
const cmakeText = readFileSync(resolve(repoRoot, 'CMakeLists.txt'), 'utf8')
const versionMatch = cmakeText.match(/^project\(encos_driver VERSION ([^)]+)\)$/m)

if (!versionMatch) throw new Error('failed to resolve project version from CMakeLists.txt')

const stagingRoot = resolve(root, 'staging')
rmSync(stagingRoot, { recursive: true, force: true })
mkdirSync(stagingRoot, { recursive: true })
const packageJson = JSON.parse(readFileSync(resolve(root, 'package.json'), 'utf8'))
packageJson.version = versionMatch[1]
writeFileSync(resolve(stagingRoot, 'package.json'), `${JSON.stringify(packageJson, null, 2)}\n`)
console.log(`generated staging package version ${packageJson.version}`)
