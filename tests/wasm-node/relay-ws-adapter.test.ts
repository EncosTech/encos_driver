import { afterEach, expect, test, vi } from 'vitest'
import { once } from 'node:events'
import type { AddressInfo } from 'node:net'
import { WebSocketServer } from 'ws'

import { MotorModel, createEncosRuntime } from '../../npm/src/index'

afterEach(() => {
  vi.restoreAllMocks()
  vi.unstubAllGlobals()
})

test('RelayWs adapter is available in WASM and is not a Fake adapter', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ session: 'test-session' }),
    }),
  )

  const runtime = await createEncosRuntime()
  const adapter = await runtime.createAdapter({
    type: 'RelayWs',
    interfaceName: 'http://127.0.0.1:9001/start?token=t&AdapterType=Fake&AdapterName=fake0',
  })

  expect(adapter.type).toBe('RelayWs')
  expect((adapter as any).fake).toBeUndefined()
  expect(adapter.ok()).toBe(false)
  expect(fetch).toHaveBeenCalledOnce()

  const code = (runtime as any).calls.fakeSeedMotor(
    adapter.handle,
    0,
    1,
    MotorModel.EC_A4310_P2,
  )
  expect(code).toBe(4)

  adapter.dispose()
  runtime.dispose()
})

test('RelayWs start response must contain a session id', async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({}),
    }),
  )

  const runtime = await createEncosRuntime()

  await expect(
    runtime.createAdapter({
      type: 'RelayWs',
      interfaceName: 'http://127.0.0.1:9001/start?token=t&AdapterType=Fake&AdapterName=fake0',
    }),
  ).rejects.toThrow(/missing session/i)

  runtime.dispose()
})

test('RelayWs response completes while Asyncify yields the event loop', async () => {
  const server = new WebSocketServer({ host: '127.0.0.1', port: 0 })
  await once(server, 'listening')
  const address = server.address() as AddressInfo
  let eventLoopProgressed = false

  server.on('connection', (socket) => {
    socket.on('message', (payload) => {
      const request = Buffer.from(payload as Buffer)
      if (request.length < 26 || request.subarray(0, 4).toString() !== 'EMR1' || request[5] === 0) {
        return
      }
      const response = Buffer.alloc(26)
      response.write('EMR1', 0)
      response[4] = 2
      response[5] = 1
      response.writeInt32LE(request.readInt32LE(8), 8)
      response.writeUInt32LE(request.readUInt32LE(12), 12)
      response[16] = request[16]
      response[17] = 8
      response[18] = 0x20
      response[24] = 50
      response[25] = 50
      const mismatchedResponse = Buffer.from(response)
      mismatchedResponse.writeUInt32LE(request.readUInt32LE(12) + 1, 12)
      setTimeout(() => {
        socket.send(mismatchedResponse)
      }, 10)
      setTimeout(() => {
        eventLoopProgressed = true
        socket.send(response)
      }, 30)
    })
  })

  const runtime = await createEncosRuntime()
  const adapter = await runtime.createAdapter({
    type: 'RelayWs',
    interfaceName: `ws://127.0.0.1:${address.port}/ws`,
  })
  try {
    const motor = adapter.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)
    const result = await motor.spdControl({ speed: 1, current: 2, feedbackType: 1 })
    expect(eventLoopProgressed).toBe(true)
    expect(result.noResponse).toBe(false)
    expect(result.feedbackType).toBe(1)
  } finally {
    adapter.dispose()
    runtime.dispose()
    for (const client of server.clients) {
      client.terminate()
    }
    await new Promise<void>((resolve) => server.close(() => resolve()))
  }
})
