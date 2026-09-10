import { afterEach, expect, test, vi } from 'vitest'

import { createEncosRuntime, MotorModel } from '../../npm/src/index'
import type { WebSerialPort, WebSerialOpenOptions } from '../../npm/src/index'

afterEach(() => vi.unstubAllGlobals())

class SerialPortFixture implements WebSerialPort {
  controller!: ReadableStreamDefaultController<Uint8Array>
  writes: Uint8Array[] = []
  onWrite?: (bytes: Uint8Array) => void
  cancel = vi.fn()
  abort = vi.fn()
  open = vi.fn(async (_options: WebSerialOpenOptions) => {})
  close = vi.fn(async () => {})
  readable = new ReadableStream<Uint8Array>({
    start: (controller) => { this.controller = controller },
    cancel: () => { this.cancel() },
  })
  writable = new WritableStream<Uint8Array>({
    write: (bytes) => {
      const copy = Uint8Array.from(bytes)
      this.writes.push(copy)
      this.onWrite?.(copy)
    },
    abort: () => { this.abort() },
  })
}

function response(id: number): Uint8Array {
  const bytes = [0xAA, id >> 8, id & 0xFF, 13, 0x20, 0, 0, 0, 0, 0, 50, 50]
  return Uint8Array.from([...bytes, bytes.reduce((sum, byte) => sum + byte, 0) & 0xFF])
}

test('Web Serial permission opens 115200 8N1 and attaches a real UsbSerial adapter', async () => {
  const port = new SerialPortFixture()
  const requestPort = vi.fn(async () => port)
  vi.stubGlobal('navigator', { serial: { requestPort } })
  const runtime = await createEncosRuntime()
  const options = { filters: [{ usbVendorId: 0x1234 }] }
  const name = await runtime.webSerial.requestPort(options)
  try {
    expect(requestPort).toHaveBeenCalledWith(options)
    expect(port.open).toHaveBeenCalledWith(expect.objectContaining({
      baudRate: 115200, dataBits: 8, stopBits: 1, parity: 'none', flowControl: 'none',
    }))
    const adapter = runtime.createAdapter({ type: 'UsbSerial', interfaceName: name })
    expect(adapter.ok()).toBe(true)
    expect(runtime.webSerial.status(name)).toMatchObject({ open: true, attached: true })
  } finally {
    runtime.dispose()
    await runtime.webSerial.close(name)
  }
  expect(port.readable.locked).toBe(false)
  expect(port.writable.locked).toBe(false)
  expect(port.close).toHaveBeenCalledOnce()
})

test('fragmented USB serial feedback completes during Asyncify and writes stay separate', async () => {
  const port = new SerialPortFixture()
  const runtime = await createEncosRuntime()
  const name = await runtime.webSerial.open(port)
  const adapter = runtime.createAdapter({ type: 'UsbSerial', interfaceName: name })
  let eventLoopProgressed = false
  try {
    const motor = adapter.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)
    await motor.spdControl({ speed: 1, current: 2, feedbackType: 0 })
    await motor.spdControl({ speed: 2, current: 2, feedbackType: 0 })
    await vi.waitFor(() => expect(port.writes).toHaveLength(2))
    for (const bytes of port.writes) {
      expect(bytes[0]).toBe(0xAA)
      expect(bytes[3]).toBe(bytes.length)
      expect(bytes.at(-1)).toBe(bytes.slice(0, -1).reduce((sum, byte) => sum + byte, 0) & 0xFF)
    }
    port.onWrite = () => {
      const packet = response(1)
      setTimeout(() => port.controller.enqueue(packet.slice(0, 3)), 5)
      setTimeout(() => {
        eventLoopProgressed = true
        port.controller.enqueue(packet.slice(3))
      }, 15)
    }
    const result = await motor.spdControl({ speed: 3, current: 2, feedbackType: 1 })
    expect(eventLoopProgressed).toBe(true)
    expect(result.noResponse).toBe(false)
    expect(result.feedbackType).toBe(1)
    expect(result.hasFeedback).toBe(true)
  } finally {
    runtime.dispose()
    await runtime.webSerial.close(name)
  }
})

test('disconnect marks the adapter unhealthy and releases stream locks', async () => {
  const port = new SerialPortFixture()
  const runtime = await createEncosRuntime()
  const name = await runtime.webSerial.open(port)
  const adapter = runtime.createAdapter({ type: 'UsbSerial', interfaceName: name })
  try {
    port.controller.error(new Error('device unplugged'))
    await vi.waitFor(() => expect(adapter.ok()).toBe(false))
    await runtime.webSerial.close(name)
    expect(port.readable.locked).toBe(false)
    expect(port.writable.locked).toBe(false)
    expect(runtime.webSerial.status(name).open).toBe(false)
  } finally {
    runtime.dispose()
    await runtime.webSerial.close(name)
  }
})

test('dispose cancels a pending read and explicit close waits for release', async () => {
  const port = new SerialPortFixture()
  const runtime = await createEncosRuntime()
  const name = await runtime.webSerial.open(port)
  runtime.createAdapter({ type: 'UsbSerial', interfaceName: name })
  runtime.dispose()
  await runtime.webSerial.close(name)
  expect(port.cancel).toHaveBeenCalledOnce()
  expect(port.readable.locked).toBe(false)
  expect(port.writable.locked).toBe(false)
  expect(port.close).toHaveBeenCalledOnce()
})

test('dispose detaches native callbacks before an already queued read resumes', async () => {
  const port = new SerialPortFixture()
  const runtime = await createEncosRuntime()
  const name = await runtime.webSerial.open(port)
  const module = runtime.module as unknown as {
    _encos_serial_port_receive: (context: number, data: number, size: number) => number
    _encos_serial_port_closed: (context: number) => void
  }
  const received = vi.spyOn(module, '_encos_serial_port_receive')
  const closed = vi.spyOn(module, '_encos_serial_port_closed')
  try {
    runtime.createAdapter({ type: 'UsbSerial', interfaceName: name })
    port.controller.enqueue(response(1))
    runtime.dispose()
    expect(runtime.webSerial.status(name)).toMatchObject({ open: false, attached: false })
    await runtime.webSerial.close(name)
    expect(received).not.toHaveBeenCalled()
    expect(closed).not.toHaveBeenCalled()
    expect(port.close).toHaveBeenCalledOnce()
  } finally {
    runtime.dispose()
    await runtime.webSerial.close(name)
    received.mockRestore()
    closed.mockRestore()
  }
})

test('repeated adapter creation shares the port until its last handle is disposed', async () => {
  const port = new SerialPortFixture()
  const runtime = await createEncosRuntime()
  const name = await runtime.webSerial.open(port)
  const adapter = runtime.createAdapter({ type: 'UsbSerial', interfaceName: name })
  try {
    const shared = runtime.createAdapter({ type: 'UsbSerial', interfaceName: name })
    expect(adapter.ok()).toBe(true)
    expect(shared.ok()).toBe(true)
    expect(port.open).toHaveBeenCalledOnce()
    adapter.dispose()
    expect(shared.ok()).toBe(true)
    expect(port.close).not.toHaveBeenCalled()
    expect(runtime.webSerial.status(name)).toMatchObject({ open: true, attached: true })
    const motor = shared.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)
    await motor.spdControl({ speed: 1, current: 2, feedbackType: 0 })
    await vi.waitFor(() => expect(port.writes).toHaveLength(1))
    shared.dispose()
    expect(runtime.webSerial.status(name)).toMatchObject({ open: false, attached: false })
    await runtime.webSerial.close(name)
    expect(port.close).toHaveBeenCalledOnce()
  } finally {
    runtime.dispose()
    await runtime.webSerial.close(name)
  }
})

test('missing browser support and denied permission reject without opening a port', async () => {
  const runtime = await createEncosRuntime()
  try {
    vi.stubGlobal('navigator', {})
    await expect(runtime.webSerial.requestPort()).rejects.toThrow(/unavailable/i)
    const denied = new DOMException('Permission denied', 'NotAllowedError')
    vi.stubGlobal('navigator', { serial: { requestPort: vi.fn().mockRejectedValue(denied) } })
    await expect(runtime.webSerial.requestPort()).rejects.toBe(denied)
  } finally {
    runtime.dispose()
  }
})

test('a port opened in one WASM module cannot be attached from another module', async () => {
  const first = await createEncosRuntime()
  const second = await createEncosRuntime()
  const port = new SerialPortFixture()
  const name = await first.webSerial.open(port)
  try {
    expect(second.webSerial.status(name).open).toBe(false)
    expect(() => second.createAdapter({ type: 'UsbSerial', interfaceName: name }))
      .toThrow(/not open|already attached/i)
    expect(first.webSerial.status(name).open).toBe(true)
  } finally {
    first.dispose()
    second.dispose()
    await first.webSerial.close(name)
  }
})
