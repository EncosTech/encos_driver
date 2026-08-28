import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const [cmakePath, packagePath] = process.argv.slice(2)
if (!cmakePath || !packagePath) throw new Error('usage: check-version.mjs CMakeLists.txt staging/package.json')
const cmake = readFileSync(cmakePath, 'utf8')
const match = cmake.match(/^project\(encos_driver VERSION ([^)]+)\)$/m)
const packageVersion = JSON.parse(readFileSync(resolve(packagePath), 'utf8')).version
if (!match || match[1] !== packageVersion) {
  throw new Error(`version mismatch: CMake=${match?.[1] ?? 'unknown'} npm=${packageVersion}`)
}
console.log(`version consistency verified: ${packageVersion}`)
