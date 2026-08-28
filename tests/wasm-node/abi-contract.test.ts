import { expect, test } from 'vitest'

import { createEncosRuntime, MotorModel } from '@encos/encos-driver'

const exportedFunctions = [
  'encos_adapter_get_bus',
  'encos_adapter_get_bus_count',
  'encos_adapter_get_bus_raw_index_at',
  'encos_adapter_ok',
  'encos_battery_allow_discharge',
  'encos_battery_clear_fault',
  'encos_battery_get_status',
  'encos_battery_request_charging',
  'encos_battery_send_passive_commands',
  'encos_bus_get_battery',
  'encos_bus_get_imu',
  'encos_bus_get_index',
  'encos_bus_get_motor_with_model',
  'encos_clear_last_error',
  'encos_create_adapter',
  'encos_create_fake_adapter',
  'encos_dispose_adapter',
  'encos_fake_enable_auto_create_motor',
  'encos_fake_get_command_bus_index',
  'encos_fake_get_command_count',
  'encos_fake_get_command_int_field',
  'encos_fake_get_command_kind',
  'encos_fake_get_command_motor_index',
  'encos_fake_get_command_number_field',
  'encos_fake_get_motor_snapshot',
  'encos_fake_get_raw_message_bus_index',
  'encos_fake_get_raw_message_can_id',
  'encos_fake_get_raw_message_count',
  'encos_fake_get_raw_message_data_byte',
  'encos_fake_get_raw_message_frame_flags',
  'encos_fake_get_raw_message_len',
  'encos_fake_inject_feedback',
  'encos_fake_inject_raw_message',
  'encos_fake_seed_motor',
  'encos_fake_set_parameter_write_policy',
  'encos_fake_set_reply_mode',
  'encos_get_last_error_code',
  'encos_get_last_error_message',
  'encos_get_version',
  'encos_imu_get_status',
  'encos_motor_cur_control',
  'encos_motor_get_float_parameter',
  'encos_motor_get_index',
  'encos_motor_get_status',
  'encos_motor_pos_control',
  'encos_motor_pvt_control',
  'encos_motor_reset_zero_pos',
  'encos_motor_set_can_timeout',
  'encos_motor_set_pos',
  'encos_motor_spd_control',
] as const

test('C ABI export snapshot preserves the Adapter-only disposal boundary', async () => {
  const runtime = await createEncosRuntime()
  const module = runtime.module as unknown as Record<string, unknown>
  const actual = Object.keys(module)
    .filter((name) => name.startsWith('_encos_'))
    .map((name) => name.slice(1))
    .sort()

  expect(actual).toEqual([...exportedFunctions].sort())
  for (const childDisposalExport of [
    'encos_dispose_bus',
    'encos_dispose_motor',
    'encos_dispose_battery',
    'encos_dispose_imu',
    'encos_dispose_pms',
  ]) {
    expect(actual).not.toContain(childDisposalExport)
  }
  runtime.dispose()
})

test('control ABI keeps Promise wrappers and five-value/five-meta result layout', async () => {
  const runtime = await createEncosRuntime()
  const adapter = runtime.createFakeAdapter({ interfaceName: 'abi-control-layout' })
  adapter.fake.seedMotor(0, 1, MotorModel.EC_A4310_P2)
  const motor = adapter.getBus(0).getMotor(1, MotorModel.EC_A4310_P2)

  const pending = motor.spdControl({ speed: 1, current: 2, feedbackType: 1 })
  expect(pending).toBeInstanceOf(Promise)
  await expect(pending).resolves.toEqual({
    ok: true,
    feedbackType: 1,
    hasFeedback: true,
    noResponse: false,
    feedback: expect.objectContaining({
      error: 0,
      position: expect.any(Number),
      speed: expect.any(Number),
      current: expect.any(Number),
      motorTemperature: expect.any(Number),
      mosTemperature: expect.any(Number),
    }),
  })

  adapter.dispose()
  runtime.dispose()
})
