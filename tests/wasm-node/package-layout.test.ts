import { resolve } from 'node:path'
import { pathToFileURL } from 'node:url'
import { readFileSync } from 'node:fs'

import { expect, test } from 'vitest'

test('published package entry exposes the shared public API from Node', async () => {
  const pkg = await import('@encos/encos-driver')

  expect(pkg).toEqual(
    expect.objectContaining({
      createEncosRuntime: expect.any(Function),
      LogLevel: expect.any(Object),
      MotorModel: expect.any(Object),
      FakeReplyMode: expect.any(Object),
      FakeCommandKind: expect.any(Object),
      FakeWritePolicy: expect.any(Object),
    }),
  )
})

test('package metadata keeps the @encos scope', () => {
  const packageJson = JSON.parse(readFileSync(resolve(process.cwd(), 'package.json'), 'utf8'))
  expect(packageJson.name).toBe('@encos/encos-driver')
})

test('published package entry loads runtime from bundled glue by default', async () => {
  const entryUrl = pathToFileURL(resolve(process.cwd(), 'dist/index.js')).href
  const pkg = await import(/* @vite-ignore */ entryUrl)
  const runtime = await pkg.createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'packaged-default-module-path' })

  expect(adapter.ok()).toBe(true)

  adapter.dispose()
  runtime.dispose()
})
