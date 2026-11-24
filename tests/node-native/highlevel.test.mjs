// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import assert from 'node:assert/strict'
import { test } from 'node:test'
import { resolve } from 'node:path'
import { pathToFileURL } from 'node:url'
import { Worker } from 'node:worker_threads'

const packageRoot = process.env.ENCOS_NODE_PACKAGE_ROOT
if (!packageRoot) throw new Error('ENCOS_NODE_PACKAGE_ROOT must point to the staged native npm package')
const entry = pathToFileURL(resolve(packageRoot, 'dist/index-native.js')).href
const {
  createEncosRuntime, MotorModel, FakeCommandKind, FakeReplyMode, FakeWritePolicy,
  MotorParameter, checkPermissions, requestPermissions,
} = await import(entry)
const model = MotorModel.EC_A4310_P2

async function fixture(t) {
  const runtime = await createEncosRuntime()
  t.after(() => runtime.dispose())
  const adapter = runtime.createFakeAdapter()
  adapter.fake.seedMotor(0, 1, model)
  const bus = adapter.getBus(0)
  const motor = await bus.getMotor(1, model)
  return { runtime, adapter, bus, motor }
}

function close(actual, expected, tolerance = 0.06) {
  assert.ok(Math.abs(actual - expected) < tolerance, `${actual} ≈ ${expected}`)
}

test('native package exports the established controls, fake helpers and Promise behavior', async t => {
  const { adapter, bus, motor } = await fixture(t)
  assert.equal(typeof checkPermissions, 'function')
  assert.equal(typeof requestPermissions, 'function')
  assert.equal(adapter.ok(), true)
  assert.equal(adapter.getBus(0), bus)
  assert.equal(await bus.getMotor(1, model), motor)
  assert.deepEqual(adapter.listBusKeys(), [{ rawIndex: 0, slaveIndex: null, busIndex: 0 }])
  const pending = motor.spdControl({ speed: 1.25, current: 2.5, feedbackType: 1 })
  assert.ok(pending instanceof Promise)
  assert.equal((await pending).noResponse, false)
  close(adapter.fake.getLastCommand().payload.speed, 1.25)
  assert.equal(adapter.fake.getLastCommand().kind, FakeCommandKind.SpdControl)
  await motor.posControl({ position: 0.5, speed: 1, current: 2, feedbackType: 2 })
  await motor.curControl({ current: 1, feedbackType: 3 })
  await motor.pvtControl({ kp: 1, kd: 1, position: 0, speed: 0, torque: 0, feedbackType: 0 })
  assert.equal(await motor.setPosition(0.25), true)
  close(await motor.readPosition(), 0.25)
  assert.equal(await motor.resetZeroPosition(), true)
  close(await motor.readPosition(), 0)
  assert.ok(Number.isFinite(await motor.readSpeed()))
  assert.ok(Number.isFinite(await motor.readCurrent()))
  assert.equal(await motor.setCanTimeout(500), true)
  assert.equal(adapter.fake.getMotorSnapshot(0, 1).canTimeoutMs, 500)
  adapter.fake.setParameterWritePolicy(0, 1, MotorParameter.CanTimeout, FakeWritePolicy.Ignore)
  assert.equal(await motor.setCanTimeout(1000), false)
  adapter.fake.injectFeedback(0, 1, { position: 0.5, speed: 1, current: 2, motorTemperature: 25 })
  close(motor.getStatus().status.position, 0.5)
  assert.ok(adapter.fake.getCommandRecords().length > 0)
})

test('battery and IMU use the same decoded API and raw fake frames', async t => {
  const { adapter, bus } = await fixture(t)
  const battery = bus.getBattery(0)
  assert.equal(bus.getBattery(0), battery)
  adapter.fake.injectRawMessage(0, 0x3f4, [1, 80, 2, 2, 200, 0, 100, 0])
  close(battery.getStatus().state.soc, 0.8, 0.0001)
  battery.clearFault()
  assert.deepEqual(adapter.fake.getLastRawCommand().data, [0x80, 0])
  battery.sendPassiveCommands({ allowShutdown: true })
  battery.requestCharging(true)
  battery.allowDischarge(true)
  assert.ok(adapter.fake.getRawMessageRecords().length >= 4)
  const imu = bus.getImu(0)
  assert.equal(bus.getImu(0), imu)
  assert.equal(imu.getStatus().acceleration, undefined)
  adapter.fake.injectRawMessage(0, 0x0cf02d59, [0, 0x7d, 0x64, 0x7d, 0x9c, 0x7c], 1)
  close(imu.getStatus().acceleration.y, 1, 0.0001)
})

test('native errors retain codes and runtime ownership remains independent', async t => {
  const { runtime, adapter, motor } = await fixture(t)
  const other = await createEncosRuntime()
  t.after(() => other.dispose())
  const independent = other.createFakeAdapter()
  independent.fake.enableAutoCreateMotor()
  const anotherMotor = await independent.getBus(0).getMotor(7, model)
  assert.equal(independent.fake.getMotorSnapshot(0, 7).model, model)
  assert.throws(() => adapter.fake.getMotorSnapshot(0, 99), error => {
    assert.equal(typeof error.code, 'number')
    assert.ok(error.message.length > 0)
    return true
  })
  adapter.dispose()
  assert.throws(() => motor.getStatus(), /disposed/)
  await assert.rejects(motor.readPosition(), /disposed/)
  runtime.dispose()
  assert.throws(() => runtime.createFakeAdapter(), /disposed/)
  assert.equal(independent.ok(), true)
  await anotherMotor.spdControl({ speed: 0, current: 0 })
})

test('waiting for hardware progresses the event loop and synchronous disposal invalidates children', async t => {
  const { adapter, motor } = await fixture(t)
  adapter.fake.setReplyMode(FakeReplyMode.Manual)
  let progressed = false
  const timer = setTimeout(() => { progressed = true }, 10)
  const result = await motor.spdControl({ speed: 1, current: 2, feedbackType: 1 })
  clearTimeout(timer)
  assert.equal(progressed, true)
  assert.equal(result.noResponse, true)
  const pending = motor.spdControl({ speed: 1, current: 2, feedbackType: 1 })
  const settled = pending.then(value => ({ value }), error => ({ error }))
  adapter.dispose()
  assert.equal(adapter.disposed, true)
  assert.throws(() => motor.getStatus(), /disposed/)
  const outcome = await settled
  if (outcome.error) assert.match(outcome.error.message, /dispos|cancel|handle/i)
  else assert.equal(outcome.value.noResponse, true)
})

test('creation disposed in flight cannot publish a live motor', async t => {
  const runtime = await createEncosRuntime()
  t.after(() => runtime.dispose())
  const adapter = runtime.createFakeAdapter()
  const pending = adapter.getBus(0).getMotor(1, model)
  const settled = assert.rejects(pending, /dispos|cancel|handle/i)
  adapter.dispose()
  await settled
})

test('Worker environments can independently use and dispose native runtimes', async () => {
  const source = `
    const { parentPort, workerData } = require('node:worker_threads');
    (async () => {
      const api = await import(workerData);
      const runtime = await api.createEncosRuntime();
      const adapter = runtime.createFakeAdapter();
      adapter.fake.seedMotor(0, 1, api.MotorModel.EC_A4310_P2);
      const motor = await adapter.getBus(0).getMotor(1, api.MotorModel.EC_A4310_P2);
      await motor.spdControl({speed: 1, current: 1});
      runtime.dispose();
      parentPort.postMessage('ok');
    })().catch(error => { throw error; });
  `
  await Promise.all(Array.from({ length: 2 }, () => new Promise((resolveWorker, reject) => {
    const worker = new Worker(source, { eval: true, workerData: entry })
    let message
    worker.on('message', value => { message = value })
    worker.on('error', reject)
    worker.on('exit', code => {
      try { assert.equal(code, 0); assert.equal(message, 'ok'); resolveWorker() }
      catch (error) { reject(error) }
    })
  })))
})

test('runtime disposal settles outstanding work and permanently closes the runtime', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter()
  const pending = adapter.getBus(0).getMotor(1, model)
  const rejected = assert.rejects(pending, /dispos|cancel|handle/i)
  runtime.dispose()
  assert.equal(adapter.disposed, true)
  assert.throws(() => runtime.createAdapter({ type: 'Fake' }), /disposed/)
  await rejected
  runtime.dispose()
})

test('terminating a Worker with an outstanding native operation leaves other runtimes usable', async t => {
  const { adapter, motor } = await fixture(t)
  const source = `
    const { parentPort, workerData } = require('node:worker_threads');
    (async () => {
      const api = await import(workerData);
      const runtime = await api.createEncosRuntime();
      const adapter = runtime.createFakeAdapter();
      const pending = adapter.getBus(0).getMotor(1, api.MotorModel.EC_A4310_P2);
      pending.catch(() => {});
      parentPort.postMessage('pending');
    })().catch(error => { throw error; });
  `
  const worker = new Worker(source, { eval: true, workerData: entry })
  await new Promise((resolveWorker, reject) => {
    worker.once('message', resolveWorker)
    worker.once('error', reject)
  })
  await worker.terminate()
  assert.equal(adapter.ok(), true)
  assert.equal((await motor.spdControl({ speed: 1, current: 1 })).ok, true)
})

test('generic adapter factories and shared adapter leases preserve the public wrapper contract', async t => {
  const runtime = await createEncosRuntime()
  t.after(() => runtime.dispose())
  const generic = runtime.createAdapter({ type: 'Fake', interfaceName: 'native-generic-fake' })
  assert.equal(generic.type, 'Fake')
  assert.equal(generic.fake, undefined)
  assert.equal(generic.ok(), true)
  const first = runtime.createFakeAdapter({ interfaceName: 'native-shared-fake' })
  const second = runtime.createFakeAdapter({ interfaceName: 'native-shared-fake' })
  first.fake.seedMotor(0, 1, model)
  const motor = await first.getBus(0).getMotor(1, model)
  first.dispose()
  assert.throws(() => motor.getStatus(), /disposed/)
  assert.equal(second.ok(), true)
  const bus = second.getBus(2, 5)
  assert.equal(second.getBus(2, 5), bus)
  assert.ok(second.listBusKeys().some(key => key.rawIndex === (2 << 16 | 5) && key.slaveIndex === 2 && key.busIndex === 5))
  assert.equal(second.fake.getMotorSnapshot(0, 1).model, model)
})
