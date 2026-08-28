import { expect, test } from 'vitest'

import { createEncosRuntime } from '../../npm/src/index'

test('Battery wrapper decodes injected BMS frames into status snapshot', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-battery-status' })
  const bus = adapter.getBus(0)
  const battery = (bus as any).getBattery(0)

  expect(battery).toBeDefined()

  ;(adapter.fake as any).injectRawMessage(0, 0x3f4, [0x01, 0x50, 0x02, 0x02, 0xc8, 0x00, 0x64, 0x00])
  ;(adapter.fake as any).injectRawMessage(0, 0x2f4, [0x19, 0x00, 0x28, 0x00, 0xfa, 0x00, 0x64, 0x00])
  ;(adapter.fake as any).injectRawMessage(0, 0x0f4, [0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
  ;(adapter.fake as any).injectRawMessage(0, 0x1f4, [0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])

  const status = battery.getStatus()
  expect(status.state).toBeDefined()
  expect(status.temp).toBeDefined()
  expect(status.activeCommands).toBeDefined()
  expect(status.error.chargerFault).toBe(true)
  expect(status.state.soc).toBeCloseTo(0.8, 4)
  expect(status.state.voltage).toBeCloseTo(5.14, 4)
  expect(status.temp.battery).toBeCloseTo(25.0, 4)
  expect(status.temp.mos).toBeCloseTo(40.0, 4)
  expect(status.activeCommands.allowCharging).toBe(true)

  adapter.dispose()
  runtime.dispose()
})

test('Battery wrapper sends passive commands as raw 0x4F4 frame', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-battery-command' })
  const battery = (adapter.getBus(0) as any).getBattery(0)

  battery.clearFault()

  const command = (adapter.fake as any).getLastRawCommand()
  expect(command.busIndex).toBe(0)
  expect(command.canId).toBe(0x4f4)
  expect(command.frameFlags).toBe(0)
  expect(command.data).toEqual([0x80, 0x00])

  adapter.dispose()
  runtime.dispose()
})
