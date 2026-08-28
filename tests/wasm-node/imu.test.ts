import { expect, test } from 'vitest'

import { createEncosRuntime } from '../../npm/src/index'

const canFrameFlagEff = 0x01

function writeU16Le(target: number[], offset: number, value: number): void {
  target[offset] = value & 0xff
  target[offset + 1] = (value >> 8) & 0xff
}

function writeBitsLe(target: number[], startBit: number, bitLen: number, value: number): void {
  for (let bit = 0; bit < bitLen; bit += 1) {
    if ((value & (1 << bit)) === 0) {
      continue
    }
    const absoluteBit = startBit + bit
    target[Math.floor(absoluteBit / 8)] |= 1 << (absoluteBit % 8)
  }
}

test('IMU wrapper decodes injected YIS130 frames into status snapshot', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-imu-status' })
  const bus = adapter.getBus(0)
  const imu = bus.getImu(0)

  expect(bus.getImu(0)).toBe(imu)

  const angular = Array.from({ length: 8 }, () => 0)
  writeBitsLe(angular, 0, 20, 512000)
  writeBitsLe(angular, 20, 20, 512128)
  writeBitsLe(angular, 40, 20, 511872)

  const quaternion = Array.from({ length: 8 }, () => 0)
  writeU16Le(quaternion, 0, 32766)
  writeU16Le(quaternion, 2, 0)
  writeU16Le(quaternion, 4, 65535)
  writeU16Le(quaternion, 6, 32768)

  adapter.fake.injectRawMessage(0, 0x0cf02d59, [0x00, 0x7d, 0x64, 0x7d, 0x9c, 0x7c], canFrameFlagEff)
  adapter.fake.injectRawMessage(0, 0x0cf02a59, angular, canFrameFlagEff)
  adapter.fake.injectRawMessage(0, 0x0cf02959, [0x00, 0x7d, 0x80, 0x7d, 0x80, 0x7c], canFrameFlagEff)
  adapter.fake.injectRawMessage(0, 0x0cf03059, quaternion, canFrameFlagEff)

  const status = imu.getStatus()
  expect(status.acceleration?.x).toBeCloseTo(0, 4)
  expect(status.acceleration?.y).toBeCloseTo(1, 4)
  expect(status.acceleration?.z).toBeCloseTo(-1, 4)
  expect(status.angularVelocity?.x).toBeCloseTo(0, 4)
  expect(status.angularVelocity?.y).toBeCloseTo(1, 4)
  expect(status.angularVelocity?.z).toBeCloseTo(-1, 4)
  expect(status.eulerAngle?.pitch).toBeCloseTo(0, 4)
  expect(status.eulerAngle?.roll).toBeCloseTo(1, 4)
  expect(status.eulerAngle?.heading).toBeCloseTo(-1, 4)
  expect(status.quaternion?.qw).toBeCloseTo(-0.0000144243, 6)
  expect(status.quaternion?.qx).toBeCloseTo(-1, 6)
  expect(status.quaternion?.qy).toBeCloseTo(1.0000627, 6)
  expect(status.quaternion?.qz).toBeCloseTo(0.0000466108, 6)

  adapter.dispose()
  runtime.dispose()
})

test('IMU wrapper exposes absent optional groups instead of zero-filled values', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'fake-imu-partial' })
  const imu = adapter.getBus(0).getImu(0)

  adapter.fake.injectRawMessage(0, 0x0cf02d59, [0x00, 0x7d, 0x64, 0x7d, 0x9c, 0x7c], canFrameFlagEff)

  const status = imu.getStatus()
  expect(status.acceleration).toBeDefined()
  expect(status.angularVelocity).toBeUndefined()
  expect(status.eulerAngle).toBeUndefined()
  expect(status.quaternion).toBeUndefined()

  adapter.dispose()
  runtime.dispose()
})
