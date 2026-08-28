import { cpSync, existsSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { execFileSync } from 'node:child_process'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const stagingRoot = resolve(root, 'staging')
const distRoot = resolve(root, 'dist')
if (!existsSync(distRoot)) throw new Error('missing npm/dist; run npm build first')
cpSync(distRoot, resolve(stagingRoot, 'dist'), { recursive: true })
execFileSync('npm', ['pack', '--pack-destination', stagingRoot], { cwd: stagingRoot, stdio: 'inherit' })
