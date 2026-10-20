// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import { expect, test } from 'vitest'

import { createEncosRuntime, FakeReplyMode, MotorModel } from '../../npm/src/index'

test('dedicated Fake adapters are isolated and expose fake tools', async () => {
  const runtime = await createEncosRuntime()
  const first = runtime.createFakeAdapter()
  const second = runtime.createFakeAdapter()

  first.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)
  const firstBus = first.getBus(0)
  const secondBus = second.getBus(0)
  const firstMotor = await firstBus.getMotor(1, MotorModel.EC_A4310_P2)
  const secondMotor = await secondBus.getMotor(1, MotorModel.EC_A4310_P2)

  expect(first.handle).not.toBe(second.handle)
  expect(first.interfaceName).not.toBe(second.interfaceName)
  expect(firstMotor.handle).not.toBe(secondMotor.handle)
  await firstMotor.spdControl({ speed: 1, current: 2, feedbackType: 0 })
  expect(first.fake.getCommandRecords().length).toBe(2)
  expect(second.fake.getCommandRecords().length).toBe(1)

  first.dispose()
  second.dispose()
  runtime.dispose()
})

test('generic Fake adapter uses normal adapter surface only', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createAdapter({ type: 'Fake', interfaceName: 'generic-fake-interface' })

  expect(adapter.type).toBe('Fake')
  expect((adapter as any).fake).toBeUndefined()
  expect(adapter.ok()).toBe(true)

  adapter.dispose()
  runtime.dispose()
})

test('slave bus keys expose raw index and decoded components', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'slave-bus-key-interface' })

  const bus = adapter.getBus(2, 5)
  expect(adapter.getBus(2, 5)).toBe(bus)
  expect(adapter.listBusKeys()).toEqual([
    {
      rawIndex: (2 << 16) | 5,
      slaveIndex: 2,
      busIndex: 5,
    },
  ])

  adapter.dispose()
  runtime.dispose()
})

test('wrapper caches bus and motor handles until adapter disposal', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'cache-test-interface' })
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)

  const busA = adapter.getBus(0)
  const busB = adapter.getBus(0)
  const motorA = await busA.getMotor(1, MotorModel.EC_A4310_P2)
  const motorB = await busB.getMotor(1, MotorModel.EC_A4310_P2)

  expect(busA).toBe(busB)
  expect(motorA).toBe(motorB)
  expect(motorA.handle).toBe(motorB.handle)

  adapter.dispose()
  expect(() => adapter.ok()).toThrow(/disposed/i)
  expect(() => motorA.getStatus()).toThrow(/disposed/i)
  runtime.dispose()
})

test('async motor APIs reject after adapter disposal', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'async-dispose-interface' })
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)
  const motor = await adapter.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)

  adapter.dispose()

  await expect(motor.spdControl({ speed: 1.0, current: 2.0, feedbackType: 0 })).rejects.toThrow(
    /disposed/i,
  )
  await expect(motor.setCanTimeout(1000, false)).rejects.toThrow(/disposed/i)
  runtime.dispose()
})

test('disposing one shared Fake adapter handle preserves the other handle lease', async () => {
  const runtime = await createEncosRuntime()
  const first = runtime.createFakeAdapter({ interfaceName: 'shared-fake-runtime-lease' })
  const second = runtime.createFakeAdapter({ interfaceName: 'shared-fake-runtime-lease' })
  const firstBus = first.getBus(0)
  const firstMotor = await firstBus.getMotor(1, MotorModel.EC_A4310_P2)

  first.dispose()
  expect(() => firstMotor.getStatus()).toThrow(/disposed/i)

  second.fake.seedMotor(0, 2, MotorModel.EC_A4310_P2)
  const secondMotor = await second.getBus(0).getMotor(2, MotorModel.EC_A4310_P2)
  expect(second.ok()).toBe(true)
  expect(() => secondMotor.getStatus()).not.toThrow()

  second.dispose()
  runtime.dispose()
})

test('synchronous adapter disposal cancels an Asyncify waiter before deferred deletion', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'asyncify-dispose-cancellation' })
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)
  adapter.fake.setReplyMode(FakeReplyMode.Manual)
  const motor = await adapter.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)

  const pending = motor.spdControl({ speed: 1, current: 2, feedbackType: 1 })
  adapter.dispose()

  const result = await pending
  expect(result.noResponse).toBe(true)
  expect(() => motor.getStatus()).toThrow(/disposed/i)
  runtime.dispose()
})

test('failed Adapter disposal restores the wrapper so deletion can be retried', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'dispose-retry' })
  const originalDispose = runtime.calls.disposeAdapter
  let attempts = 0
  runtime.calls.disposeAdapter = (...args: number[]) => {
    attempts += 1
    return attempts === 1 ? 5 : originalDispose(...args)
  }

  expect(() => adapter.dispose()).toThrow()
  expect(adapter.disposed).toBe(false)
  expect(adapter.ok()).toBe(true)

  adapter.dispose()
  expect(attempts).toBe(2)
  runtime.dispose()
})


test('motor creation waits for firmware timeout without blocking the event loop', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'firmware-timeout' })
  let progressed = false
  const timer = setTimeout(() => { progressed = true }, 0)
  try {
    const bus = adapter.getBus(0)
    const pending = bus.getMotor(1, MotorModel.EC_A4310_P2)
    const repeated = bus.getMotor(1, MotorModel.EC_A4310_P2)
    expect(() => adapter.dispose()).toThrow(/dispose/i)
    expect(adapter.disposed).toBe(false)
    await expect(bus.getMotor(2, MotorModel.EC_A4310_P2)).rejects.toThrow(/operation.*progress/i)
    const motor = await pending
    expect(await repeated).toBe(motor)
    expect(progressed).toBe(true)
    const other = await bus.getMotor(2, MotorModel.EC_A4310_P2)
    expect(other.index).toBe(2)
    expect(await bus.getMotor(1, MotorModel.EC_A4310_P2)).toBe(motor)
    await motor.spdControl({ speed: 1, current: 2, feedbackType: 0 })
  } finally {
    clearTimeout(timer)
    runtime.dispose()
  }
})


test('motor creation rejects when its adapter is disposed before wrapper publication', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter()
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)
  const pending = adapter.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)
  adapter.dispose()
  await expect(pending).rejects.toThrow(/disposed/i)
  runtime.dispose()
})

test.each(['creation', 'control'])('rejects overlapping Asyncify calls with %s first', async (first) => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter()
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)
  const bus = adapter.getBus(0)
  const motor = await bus.getMotor(1, MotorModel.EC_A4310_P2)
  adapter.fake.setReplyMode(FakeReplyMode.Manual)
  const create = () => bus.getMotor(2, MotorModel.EC_A4310_P2)
  const control = () => motor.spdControl({ speed: 1, current: 2, feedbackType: 1 })
  try {
    const pending = first === 'creation' ? create() : control()
    await expect(first === 'creation' ? control() : create()).rejects.toThrow(/operation.*progress/i)
    await pending
    expect((await control()).noResponse).toBe(true)
    expect((await create()).index).toBe(2)
  } finally {
    runtime.dispose()
  }
})
