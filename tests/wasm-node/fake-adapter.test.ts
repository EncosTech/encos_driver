import { expect, test } from 'vitest'

import {
  FakeCommandKind,
  FakeReplyMode,
  FakeWritePolicy,
  MotorModel,
  MotorParameter,
  createEncosRuntime,
} from '../../npm/src/index'

test('seeded Fake motor records and decodes speed control commands', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-spd-command' })
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)

  const motor = adapter.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)
  await motor.spdControl({ speed: 1.25, current: 2.5, feedbackType: 0 })

  const commands = adapter.fake.getCommandRecords()
  expect(commands.length).toBe(1)
  expect(commands[0].kind).toBe(FakeCommandKind.SpdControl)
  expect(commands[0].busIndex).toBe(0)
  expect(commands[0].motorIndex).toBe(1)
  expect(commands[0].payload.feedbackType).toBe(0)
  expect(Math.abs(commands[0].payload.speed - 1.25)).toBeLessThan(0.02)
  expect(Math.abs(commands[0].payload.current - 2.5)).toBeLessThan(0.06)

  const snapshot = adapter.fake.getMotorSnapshot(0, 1)
  expect(Math.abs(snapshot.speedRadS - commands[0].payload.speed)).toBeLessThan(0.02)

  adapter.dispose()
  runtime.dispose()
})

test('calibration APIs expose current control and position setters', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-calibration-apis' })
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)

  const motor = adapter.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)
  await motor.curControl({ current: -1.5, feedbackType: 1 })

  let command = adapter.fake.getLastCommand()
  expect(command!.kind).toBe(FakeCommandKind.CurControl)
  expect(Math.abs(command!.payload.current + 1.5)).toBeLessThan(0.06)
  expect(command!.payload.feedbackType).toBe(1)

  expect(await motor.setPosition(0.25)).toBe(true)
  command = adapter.fake.getLastCommand()
  expect(command!.kind).toBe(FakeCommandKind.SetPos)
  expect(Math.abs(command!.payload.position - 0.25)).toBeLessThan(0.02)
  expect(Math.abs((await motor.readPosition()) - 0.25)).toBeLessThan(0.02)

  expect(await motor.resetZeroPosition(true)).toBe(true)
  command = adapter.fake.getLastCommand()
  expect(command!.kind).toBe(FakeCommandKind.ResetZeroPos)
  expect(Math.abs(await motor.readPosition())).toBeLessThan(0.02)

  const speed = await motor.readSpeed()
  expect(Number.isFinite(speed)).toBe(true)

  adapter.dispose()
  runtime.dispose()
})

test('speed control supports feedback type 2 and 3', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-spd-feedback-types' })
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)

  const motor = adapter.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)
  let extraSleepOccurred = false
  const extraSleepTimer = setTimeout(() => {
    extraSleepOccurred = true
  }, 0)
  const feedback2 = await motor.spdControl({ speed: 1.0, current: 5.0, feedbackType: 2 })
  clearTimeout(extraSleepTimer)
  expect(extraSleepOccurred).toBe(false)
  expect(feedback2.noResponse).toBe(false)
  expect(feedback2.feedback!.error).toBe(0)
  expect(Math.abs(feedback2.feedback!.current - 5.0)).toBeLessThan(0.06)
  expect(adapter.fake.getLastCommand()!.payload.feedbackType).toBe(2)

  const feedback3 = await motor.spdControl({ speed: 1.5, current: 4.0, feedbackType: 3 })
  expect(feedback3.noResponse).toBe(false)
  expect(feedback3.feedback!.error).toBe(0)
  expect(Math.abs(feedback3.feedback!.speed - 1.5)).toBeLessThan(0.02)
  expect(Math.abs(feedback3.feedback!.current - 4.0)).toBeLessThan(0.06)
  expect(adapter.fake.getLastCommand()!.payload.feedbackType).toBe(3)

  adapter.dispose()
  runtime.dispose()
})

test('manual speed control records command and returns no response', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-spd-manual' })
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)
  adapter.fake.setReplyMode(FakeReplyMode.Manual)

  const motor = adapter.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)
  let eventLoopProgressed = false
  const progressTimer = setTimeout(() => {
    eventLoopProgressed = true
  }, 25)
  const result = await motor.spdControl({ speed: 1.0, current: 2.0, feedbackType: 1 })
  clearTimeout(progressTimer)

  expect(eventLoopProgressed).toBe(true)
  expect(result.noResponse).toBe(true)
  expect(result.feedback!.error).toBe(255)
  const command = adapter.fake.getLastCommand()
  expect(command!.kind).toBe(FakeCommandKind.SpdControl)
  expect(command!.payload.feedbackType).toBe(1)
  expect(Math.abs(command!.payload.speed - 1.0)).toBeLessThan(0.02)
  expect(Math.abs(command!.payload.current - 2.0)).toBeLessThan(0.06)

  adapter.fake.injectFeedback(0, 1, {
    feedbackType: 1,
    error: 0,
    position: 0,
    speed: 0,
    current: 0,
    motorTemperature: 25,
    mosTemperature: 25,
  })
  expect(motor.getStatus().hasValue).toBe(true)

  adapter.dispose()
  runtime.dispose()
})

test('position control supports feedback type 2 and 3', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-pos-feedback-types' })
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)

  const motor = adapter.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)
  const feedback2 = await motor.posControl({
    position: 0.25,
    speed: 1.0,
    current: 2.0,
    feedbackType: 2,
  })
  expect(feedback2.noResponse).toBe(false)
  expect(feedback2.feedback!.error).toBe(0)
  expect(Math.abs(feedback2.feedback!.position - 0.25)).toBeLessThan(0.02)
  expect(Math.abs(feedback2.feedback!.current - 2.0)).toBeLessThan(0.06)
  expect(adapter.fake.getLastCommand()!.payload.feedbackType).toBe(2)

  const feedback3 = await motor.posControl({
    position: 0.5,
    speed: 1.5,
    current: 2.5,
    feedbackType: 3,
  })
  expect(feedback3.noResponse).toBe(false)
  expect(feedback3.feedback!.error).toBe(0)
  expect(Math.abs(feedback3.feedback!.speed - 1.5)).toBeLessThan(0.02)
  expect(Math.abs(feedback3.feedback!.current - 2.5)).toBeLessThan(0.06)
  expect(adapter.fake.getLastCommand()!.payload.feedbackType).toBe(3)

  adapter.dispose()
  runtime.dispose()
})

test('parameter write policy can ignore CAN timeout ack', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-policy' })
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)
  adapter.fake.setParameterWritePolicy(
    0,
    1,
    MotorParameter.CanTimeout,
    FakeWritePolicy.Ignore,
  )

  const motor = adapter.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)
  expect(await motor.setCanTimeout(1000, true)).toBe(false)

  const command = adapter.fake.getLastCommand()
  expect(command!.kind).toBe(FakeCommandKind.SetParameter)
  expect(command!.payload.parameter).toBe(MotorParameter.CanTimeout)

  adapter.dispose()
  runtime.dispose()
})

test('PVT control decodes command with seeded motor ranges', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-pvt-command' })
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A10010_P2)

  const motor = adapter.getBus(0).getMotor(1, MotorModel.EC_A10010_P2)
  const ranges = adapter.fake.getMotorSnapshot(0, 1).ranges
  const torque = ranges.torque.max * 0.75
  await motor.pvtControl({
    kp: ranges.kp.max,
    kd: ranges.kd.max,
    position: ranges.position.max,
    speed: ranges.speed.max,
    torque,
    feedbackType: 0,
  })

  const command = adapter.fake.getLastCommand()
  expect(command!.kind).toBe(FakeCommandKind.PVTControl)
  expect(Math.abs(command!.payload.kp - ranges.kp.max)).toBeLessThan(0.05)
  expect(Math.abs(command!.payload.kd - ranges.kd.max)).toBeLessThan(0.05)
  expect(Math.abs(command!.payload.position - ranges.position.max)).toBeLessThan(0.02)
  expect(Math.abs(command!.payload.speed - ranges.speed.max)).toBeLessThan(0.02)
  expect(Math.abs(command!.payload.torque - torque)).toBeLessThan(0.2)

  adapter.dispose()
  runtime.dispose()
})

test('listBusKeys includes created and seeded Fake buses', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-list-buses' })
  const heldBus = adapter.getBus(3)
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)
  adapter.fake.seedMotor(5, 2, MotorModel.EC_A4310_P2)

  expect(
    adapter.listBusKeys().map((key) => key.rawIndex).sort((a, b) => a - b),
  ).toEqual([0, 3, 5])
  expect(adapter.getBus(3)).toBe(heldBus)

  adapter.dispose()
  runtime.dispose()
})

test('auto-create is disabled by default but commands are still recorded', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-auto-create-disabled' })

  const motor = adapter.getBus(0).getMotor(7, MotorModel.EC_A4310_P2)
  await motor.posControl({ position: 1.0, speed: 2.0, current: 3.0, feedbackType: 0 })

  const command = adapter.fake.getLastCommand()
  expect(command!.kind).toBe(FakeCommandKind.PosControl)
  expect(command!.motorIndex).toBe(7)
  expect(() => adapter.fake.getMotorSnapshot(0, 7)).toThrow(/snapshot|fake motor/i)

  adapter.dispose()
  runtime.dispose()
})

test('injected feedback updates adapter status cache', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-inject-feedback' })
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)

  const motor = adapter.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)
  adapter.fake.injectFeedback(0, 1, {
    feedbackType: 1,
    error: 0,
    position: 0.5,
    speed: 1.25,
    current: 2.0,
    motorTemperature: 25.0,
    mosTemperature: 30.0,
  })

  const cached = motor.getStatus()
  expect(cached.hasValue).toBe(true)
  expect(Math.abs(cached.status!.position - 0.5)).toBeLessThan(0.02)
  expect(Math.abs(cached.status!.speed - 1.25)).toBeLessThan(0.02)
  expect(Math.abs(cached.status!.current - 2.0)).toBeLessThan(0.06)

  adapter.dispose()
  runtime.dispose()
})

test('auto-create creates snapshot and replies unless reply mode is manual', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-auto-create' })
  adapter.fake.enableAutoCreateMotor()

  const motor = adapter.getBus(0).getMotor(7, MotorModel.EC_A4310_P2)
  await motor.posControl({ position: 1.25, speed: 2.0, current: 3.0, feedbackType: 1 })

  const snapshot = adapter.fake.getMotorSnapshot(0, 7)
  expect(snapshot.model).toBe(MotorModel.EC_A4310_P2)
  expect(Math.abs(snapshot.positionRad - 1.25)).toBeLessThan(0.02)

  const status = motor.getStatus()
  expect(status.hasValue).toBe(true)
  expect(Math.abs(status.status!.position - 1.25)).toBeLessThan(0.02)

  const manualAdapter = runtime.createFakeAdapter({ interfaceName: 'fake-auto-create-manual' })
  manualAdapter.fake.enableAutoCreateMotor()
  manualAdapter.fake.setReplyMode(FakeReplyMode.Manual)

  const manualMotor = manualAdapter.getBus(0).getMotor(8, MotorModel.EC_A4310_P2)
  await manualMotor.posControl({ position: 0.5, speed: 1.0, current: 2.0, feedbackType: 1 })

  expect(manualAdapter.fake.getMotorSnapshot(0, 8).model).toBe(MotorModel.EC_A4310_P2)
  expect(manualMotor.getStatus().hasValue).toBe(false)

  adapter.dispose()
  manualAdapter.dispose()
  runtime.dispose()
})
