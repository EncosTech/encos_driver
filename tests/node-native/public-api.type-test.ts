// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import {
  DexHandStatus, LogLevel, MotorModel, MotorParameter,
  type DexHandReply, type DexHandSamplePoints, type DexHandWrapper, type MotorEncoderAngle,
  type MotorProperties, type MotorStatus, type Range, type Runtime,
} from '../../npm/src/index-native.js'

/** 编译期验证原生公共接口的参数和返回类型。 */
export async function checkNativeTypes(runtime: Runtime): Promise<void> {
  const logger = runtime.createLogger('types', LogLevel.Off)
  logger.info('{}', 42)
  const adapter = runtime.createAdapter({ type: 'Fake', extend: {} })
  const bus = adapter.getBus(0)
  const hand: DexHandWrapper = bus.getDexHand(1)
  const id: DexHandReply<number> = await hand.getBaseId({ timeoutMs: 30 })
  const samplePoints: DexHandReply<DexHandSamplePoints> = await hand.getSamplePoints({ canfd: true })
  if (id.status === DexHandStatus.Success && id.value !== undefined) await hand.setBaseId(id.value)
  if (samplePoints.value) await hand.setSamplePoints(samplePoints.value)
  runtime.deleteDexHand(hand)
  const properties: MotorProperties = runtime.getMotorModelRanges(MotorModel.EC_A8116_H32_100_B)
  const motor = await bus.getMotor(1, { properties, canfd: true })
  const range: Range = await motor.getParameter(MotorParameter.PVTPosRange)
  const angle: MotorEncoderAngle = await motor.getParameter(MotorParameter.EncoderAngle)
  const position: number = await motor.getParameter(MotorParameter.Position)
  motor.setOnStatus((status: MotorStatus) => { void status.position })
  await motor.setPvtPosRange(range)
  await motor.torControl({ torque: position, feedbackType: 0, feedback: 1 })
  const fake = adapter.getFakeAdapterControl()
  if (fake) {
    const snapshot = fake.createSnapshot(MotorModel.EC_A4310_P2)
    snapshot.calAngleDeg = angle.calAngle
    fake.seedMotor(0, 2, snapshot)
    fake.setDecodedCommandObserver(record => {
      if (record.kind === 7) { const enabled: boolean = record.payload.enabled; void enabled }
      if (record.kind === 12) { const bytes: number[] = record.payload.rawValue; void bytes }
    })
  }
  runtime.createLogWriter('types', ['n']).write(1n)
}
