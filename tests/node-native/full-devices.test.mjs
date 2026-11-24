// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import test from 'node:test'
import { resolve } from 'node:path'
import { pathToFileURL } from 'node:url'
import { setTimeout as delay } from 'node:timers/promises'

const root = process.env.ENCOS_NODE_PACKAGE_ROOT
if (!root) throw new Error('ENCOS_NODE_PACKAGE_ROOT must point to the staged native npm package')
const { createEncosRuntime, MotorModel, CanFrameFlag, DexHandStatus } = await import(pathToFileURL(resolve(root, 'dist/index-native.js')).href)
const model = MotorModel.EC_A4310_P2

async function fixture(t, newFirmware = false) {
  const runtime = await createEncosRuntime()
  t.after(() => runtime.dispose())
  const adapter = runtime.createFakeAdapter()
  adapter.fake.seedMotor(0, 1, model)
  if (newFirmware) runtime._invoke('fake.seedMotor', { handle: adapter.handle, busIndex: 0, motorIndex: 1, snapshot: { model, fwVersion: 0x26010100 } })
  const bus = adapter.getBus(0)
  const motor = await bus.getMotor(1, model)
  const invoke = (name, args = {}) => runtime._invoke(name, args)
  const invokeAsync = (name, args = {}) => runtime._invokeAsync(name, args)
  const m = (name, args = {}) => invoke(`motor.${name}`, { handle: motor.handle, ...args })
  const ma = (name, args = {}) => invokeAsync(`motor.${name}`, { handle: motor.handle, ...args })
  return { runtime, adapter, bus, motor, invoke, invokeAsync, m, ma }
}

async function until(predicate) {
  const deadline = Date.now() + 2000
  while (!predicate() && Date.now() < deadline) await delay(5)
  assert.ok(predicate(), 'native callback should arrive on the JS event loop')
}

function close(actual, expected, tolerance = 0.01) {
  assert.ok(Math.abs(actual - expected) < tolerance, `${actual} ≈ ${expected}`)
}

test('all 21 typed motor queries preserve scalar and structured result types', async t => {
  const { ma } = await fixture(t, true)
  for (const parameter of [1, 2, 3, 4, 22, 29, 31, 37, 48, 49, 50]) {
    assert.equal(typeof await ma('getParameter', { parameter }), 'number', `parameter ${parameter}`)
  }
  for (const parameter of [23, 24, 25, 26, 27, 28]) {
    const value = await ma('getParameter', { parameter })
    assert.deepEqual(Object.keys(value).sort(), ['max', 'min'])
    assert.equal(typeof value.min, 'number')
    assert.equal(typeof value.max, 'number')
  }
  for (const parameter of [32, 33, 34]) {
    const value = await ma('getParameter', { parameter })
    assert.deepEqual(Object.keys(value).sort(), parameter === 34 ? ['kd', 'kp'] : ['ki', 'kp'])
  }
  const encoder = await ma('getParameter', { parameter: 39 })
  assert.deepEqual(Object.keys(encoder).sort(), ['calAngle', 'calFlag', 'rawAngle'])
})

test('motor properties, frame modes, range writes, gains and ID migration work through native dispatch', async t => {
  const { bus, motor, invoke, m, ma } = await fixture(t)
  const properties = m('getProperties')
  m('setProperties', { properties: { ...properties, position: { min: -1, max: 1 }, kt: 0.75 } })
  assert.deepEqual(m('getProperties').position, { min: -1, max: 1 })
  assert.equal(m('getProperties').kt, properties.kt)
  for (const mode of ['CanFd', 'CanEff']) {
    m(`enable${mode}`)
    assert.equal(m(`is${mode}Enabled`), true)
    m(`disable${mode}`)
    assert.equal(m(`is${mode}Enabled`), false)
  }
  for (const [name, range, parameter] of [
    ['Kp', { min: 0, max: 200 }, 23], ['Kd', { min: 0, max: 20 }, 24],
    ['Pos', { min: -2, max: 2 }, 25], ['Spd', { min: -4, max: 4 }, 26],
    ['Tor', { min: -5, max: 5 }, 27], ['Cur', { min: -6, max: 6 }, 28],
  ]) {
    assert.equal(await ma(`setPvt${name}Range`, { range }), true)
    const actual = await ma('getParameter', { parameter })
    close(actual.min, range.min)
    close(actual.max, range.max)
  }
  for (const [name, args, parameter] of [
    ['setCurPi', { kp: 0.1, ki: 0.1 }, 32], ['setSpdPi', { kp: 0.1, ki: 0.1 }, 33],
    ['setPosPd', { kp: 0.1, kd: 0.1 }, 34],
  ]) {
    assert.equal(await ma(name, args), true)
    close((await ma('getParameter', { parameter })).kp, 0.1)
  }
  assert.equal(await ma('setCanTimeout', { timeoutMs: 250 }), true)
  assert.equal(await ma('getParameter', { parameter: 31 }), 250)
  assert.equal(await ma('setPosition', { position: 0.5 }), true)
  close(await ma('getParameter', { parameter: 1 }), 0.5)
  assert.equal(await ma('setId', { newIndex: 2 }), true)
  assert.equal(invoke('bus.selectMotor', { handle: bus.handle, index: 2 }), motor.handle)
  assert.equal(invoke('bus.selectMotor', { handle: bus.handle, index: 1 }), null)
})

test('control results retain feedback shape and no-wait wire feedback is independent', async t => {
  const { adapter, m, ma } = await fixture(t)
  const commands = [
    ['posControl', { position: 0.5, speed: 0, current: 2 }],
    ['spdControl', { speed: 0, current: 2 }],
    ['curControl', { current: 1 }], ['torControl', { torque: 1 }],
    ['stop', { mode: 2, current: 1 }],
  ]
  for (const [name, args] of commands) {
    for (const feedbackType of [1, 2, 3]) {
      const result = await ma(name, { ...args, feedbackType })
      assert.equal(result.ok, true)
      assert.equal(result.feedbackType, feedbackType)
      assert.equal(result.hasFeedback, true)
      assert.equal(result.noResponse, false)
      if (feedbackType === 2) assert.ok(Number.isNaN(result.feedback.speed))
      if (feedbackType === 3) assert.ok(Number.isNaN(result.feedback.position))
    }
    const result = await ma(name, { ...args, feedbackType: 0, feedback: 1 })
    assert.equal(result.hasFeedback, false)
    assert.equal(result.feedback, undefined)
    assert.equal(adapter.fake.getLastDecodedCommand().payload.feedbackType, 1)
  }
  const pvt = { kp: 1, kd: 1, position: 0, speed: 0, torque: 0 }
  assert.equal((await ma('pvtControl', { ...pvt, feedbackType: 1 })).hasFeedback, true)
  m('setMaxStatusLifeCycle', { maxLifeCycle: 3 })
  m('setStatusMedianFilterWindowSize', { windowSize: 1 })
  m('setStatusLimitFilterMaxDeltas', { deltas: { position: NaN, speed: Infinity } })
})

test('integral and enum parameters reject truncation, overflow and invalid variants', async t => {
  const { m } = await fixture(t)
  for (const newIndex of [-1, 1.5, 65536, NaN, Infinity]) {
    assert.throws(() => m('setId', { newIndex }), /integer|range/i)
  }
  for (const parameter of [0, 257, -255, 1.5]) {
    assert.throws(() => m('getParameter', { parameter }), /parameter|integer/i)
  }
  assert.throws(() => m('setCanTimeout', { timeoutMs: 65536 }), /integer|range/i)
  assert.throws(() => m('setPvtKpRange', { range: { min: 0, max: 65536 } }), /integer|range/i)
  assert.throws(() => m('setCommunicationMode', { mode: 3 }), /mode/i)
  assert.throws(() => m('stop', { mode: 1, current: 0 }), /mode/i)
  assert.throws(() => m('pvtControl', { feedbackType: 2 }), /PVT/i)
  assert.throws(() => m('curControl', { current: 0, feedbackType: 4 }), /feedback/i)
})

test('motor and battery callbacks retain repeated frames and unregister cancels delivery', async t => {
  const { runtime, adapter, bus, motor, invoke } = await fixture(t)
  const motorEvents = []
  runtime.backend.subscribe('motor.onStatus', { handle: motor.handle }, value => motorEvents.push(value))
  adapter.fake.injectFeedback(0, 1, { position: 0.5, speed: 0, current: 1, motorTemperature: 25 })
  await until(() => motorEvents.length === 1)
  close(motorEvents[0].position, 0.5)
  const battery = invoke('bus.getBattery', { handle: bus.handle, index: 0 })
  const batteryEvents = []
  runtime.backend.subscribe('battery.onStatus', { handle: battery }, value => batteryEvents.push(value))
  const frame = [1, 80, 2, 2, 200, 0, 100, 0]
  adapter.fake.injectRawMessage(0, 0x3f4, frame)
  adapter.fake.injectRawMessage(0, 0x3f4, frame)
  await until(() => batteryEvents.length === 2)
  close(batteryEvents[0].state.soc, 0.8)
  assert.equal(batteryEvents[0].temp, undefined)
  runtime.backend.subscribe('battery.onStatus', { handle: battery }, null)
  adapter.fake.injectRawMessage(0, 0x3f4, frame)
  await delay(25)
  assert.equal(batteryEvents.length, 2)
  adapter.fake.injectRawMessage(0, 0x2f4, [0, 0, 0, 0, 100, 0, 200, 0])
  adapter.fake.injectRawMessage(0, 0x0f4, [255, 7])
  adapter.fake.injectRawMessage(0, 0x1f4, [63])
  const status = invoke('battery.getStatus', { handle: battery })
  assert.deepEqual(Object.keys(status.temp).sort(), ['battery', 'chargeCurrent', 'dischargeCurrent', 'mos'])
  assert.ok(Object.entries(status.error).filter(([key]) => key !== 'commTimeout').every(([, value]) => value === true))
  assert.ok(Object.values(status.activeCommands).every(value => value === true))
  invoke('battery.sendPassiveCommands', { handle: battery, commands: { clearFault: true, debug: true } })
  assert.deepEqual(adapter.fake.getLastRawCommand().data, [0x80, 2])
})

test('IMU and PMS preserve full status groups, callback cadence and PMS expiry', async t => {
  const { runtime, adapter, bus, invoke } = await fixture(t)
  const imu = invoke('bus.getImu', { handle: bus.handle, index: 0 })
  const imuEvents = []
  runtime.backend.subscribe('imu.onStatus', { handle: imu }, value => imuEvents.push(value))
  adapter.fake.injectRawMessage(0, 0x0cf02d59, [0, 0x7d, 0x64, 0x7d, 0x9c, 0x7c], 1)
  await until(() => imuEvents.length === 1)
  close(imuEvents[0].acceleration.y, 1)
  assert.equal(imuEvents[0].quaternion, undefined)
  adapter.fake.injectRawMessage(0, 0x0cf02a59, Array(8).fill(0), 1)
  adapter.fake.injectRawMessage(0, 0x0cf02959, Array(6).fill(0), 1)
  adapter.fake.injectRawMessage(0, 0x0cf03059, Array(8).fill(0), 1)
  await until(() => imuEvents.length === 4)
  const imuStatus = invoke('imu.getStatus', { handle: imu })
  assert.deepEqual(Object.keys(imuStatus).sort(), ['acceleration', 'angularVelocity', 'eulerAngle', 'quaternion'])
  assert.deepEqual(Object.keys(imuStatus.angularVelocity).sort(), ['x', 'y', 'z'])
  assert.deepEqual(imuStatus.eulerAngle, { pitch: -250, roll: -250, heading: -250 })
  assert.deepEqual(imuStatus.quaternion, { qw: -1, qx: -1, qy: -1, qz: -1 })
  const pms = invoke('bus.getPms', { handle: bus.handle })
  const pmsEvents = []
  runtime.backend.subscribe('pms.onStatus', { handle: pms }, value => pmsEvents.push(value))
  assert.equal(invoke('pms.getStatus', { handle: pms }), null)
  adapter.fake.injectRawMessage(0, 0x18f0fff2, [0x15, 80, 0xc0, 0x12, 0x9c, 0xff, 100, 0], 1)
  adapter.fake.injectRawMessage(0, 0x18f1fff2, [100, 0, 200, 0, 44, 1, 144, 1], 1)
  assert.equal(invoke('pms.getStatus', { handle: pms }), null)
  adapter.fake.injectRawMessage(0, 0x18f2fff2, [244, 1, 88, 2, 188, 2, 32, 3], 1)
  await until(() => pmsEvents.length === 1)
  const status = invoke('pms.getStatus', { handle: pms })
  assert.deepEqual(status.v48ChannelEnabled, [true, false, true, false, true, false])
  assert.equal(status.batterySoc, 80)
  close(status.batteryVoltage, 48)
  close(status.batteryCurrent, -1)
  assert.deepEqual(status.v48Currents, [1, 2, 3, 4, 5, 6])
  assert.deepEqual(status.v19Currents, [7, 8])
  invoke('pms.sendCommand', { handle: pms, command: 0x0201 })
  assert.deepEqual(adapter.fake.getLastRawCommand().data, [1, 2, 0, 0, 0, 0, 0, 0])
  assert.throws(() => invoke('pms.sendCommand', { handle: pms, command: 0x0101 }), /same channel/i)
  await delay(2050)
  assert.equal(invoke('pms.getStatus', { handle: pms }), null)
})

test('glove preserves 50 optional angles and emits only complete update cycles', async t => {
  const { runtime, adapter, invoke, invokeAsync } = await fixture(t)
  const glove = invoke('adapter.getGlove', { handle: adapter.handle, slaveIndex: 1 })
  const events = []
  runtime.backend.subscribe('glove.onStatus', { handle: glove }, value => events.push(value))
  assert.deepEqual(invoke('glove.getStatus', { handle: glove }), Array.from({ length: 5 }, () => Array(10).fill(null)))
  for (let finger = 0; finger < 5; ++finger) {
    for (let encoder = 0; encoder < 10; ++encoder) {
      adapter.fake.injectRawMessage((1 << 16) | finger, 0x8100 + finger * 10 + encoder, [0xff, 0xff, encoder === 0 ? 0 : 1], 1)
    }
  }
  await until(() => events.length === 1)
  for (const finger of events[0]) {
    assert.equal(finger[0], null)
    for (const angle of finger.slice(1)) close(angle, 2 * Math.PI)
  }
  assert.throws(() => invoke('glove.calibrateByMask', { handle: glove, fingerIndex: 5, encoderMask: 1 }), /finger/i)
  assert.throws(() => invoke('glove.calibrateByMask', { handle: glove, fingerIndex: 0, encoderMask: 1024 }), /mask/i)
  assert.equal(await invokeAsync('glove.calibrateByMask', { handle: glove, fingerIndex: 0, encoderMask: 1 }), 0)
  assert.equal(await invokeAsync('glove.calibrateAll', { handle: glove }), 0)
})

test('DexHand requests preserve the address, CAN format, and optional reply value', async t => {
  const runtime = await createEncosRuntime()
  t.after(() => runtime.dispose())
  const adapter = runtime.createFakeAdapter()
  const bus = adapter.getBus(0)
  const hand = bus.getDexHand()
  assert.equal(bus.getDexHand(1), hand)
  const broadcast = bus.getDexHand(0)
  const options = { timeoutMs: 5 }
  for (const [reply, id, data, flags] of [
    [() => broadcast.getBaseId(options), 0, [0xf0, 1], 0],
    [() => hand.setBaseId(247, options), 1, [0xf8, 1, 247], 0],
    [() => hand.getSamplePoints(options), 1, [0xf0, 0], 0],
    [() => hand.setSamplePoints({ nominal: 750, data: 875 }, { ...options, canfd: true }), 1,
      [0xf8, 0, 0xee, 2, 0x6b, 3], CanFrameFlag.FdMask],
  ]) {
    assert.deepEqual(await reply(), { status: DexHandStatus.Timeout })
    const sent = adapter.fake.getRawSentMessages().at(-1).data
    assert.deepEqual({ ...sent, data: sent.data.slice(0, sent.len) },
      { id, data, frameFlags: flags, len: data.length })
  }
  assert.throws(() => bus.getDexHand(248), /DexHand request ID/)
  await assert.rejects(hand.setBaseId(0), /DexHand base ID/)
  await assert.rejects(hand.setSamplePoints({ nominal: 1.5, data: 875 }), /integer/)
  await assert.rejects(hand.getBaseId({ timeoutMs: -1 }), /range/)

  const aliasAdapter = runtime.createFakeAdapter({ interfaceName: adapter.interfaceName })
  const alias = aliasAdapter.getBus(0).getDexHand(1)
  assert.equal(runtime.deleteDexHand(hand), true)
  assert.equal(runtime.deleteDexHand(hand), false)
  await assert.rejects(alias.getBaseId(options), /invalid.*handle/)
  const fresh = bus.getDexHand(1)
  assert.notEqual(fresh, hand)
  assert.notEqual(aliasAdapter.getBus(0).getDexHand(1), alias)
  bus.dispose()
  await assert.rejects(fresh.getBaseId(options), /disposed/)

  const reopened = adapter.getBus(0).getDexHand(1)
  adapter.dispose()
  await assert.rejects(reopened.getBaseId(options), /disposed/)
})
