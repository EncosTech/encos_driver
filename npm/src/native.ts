// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import type { CFunction, WasmModule } from './types.js'

export class EncosWasmError extends Error {
  code: number

  constructor(module: WasmModule, code: number, fallback: string) {
    const message = module.UTF8ToString(module._encos_get_last_error_message?.() ?? 0) || fallback
    super(message)
    this.name = 'EncosWasmError'
    this.code = code
  }
}

export function assertOk(runtime: { module: WasmModule }, code: number, fallback: string): void {
  if (code !== 0) {
    throw new EncosWasmError(runtime.module, code, fallback)
  }
}

export function allocU32(module: WasmModule): number {
  return module._malloc(4)
}

export function getU32(module: WasmModule, ptr: number): number {
  return module.getValue(ptr, 'i32') >>> 0
}

export function getI32(module: WasmModule, ptr: number): number {
  return module.getValue(ptr, 'i32')
}

export function createRuntimeCalls(module: WasmModule): Record<string, CFunction> {
  let asyncCallActive = false
  const wrap: WasmModule['cwrap'] = (name, returnType, argTypes, options) => {
    const call = module.cwrap(name, returnType, argTypes, options)
    if (!options?.async) {
      return call
    }
    return async (...args: any[]) => {
      if (asyncCallActive) {
        throw new Error('another async operation is in progress; await it before starting a new one')
      }
      asyncCallActive = true
      try {
        return await call(...args)
      } finally {
        asyncCallActive = false
      }
    }
  }
  return {
    createAdapter: wrap('encos_create_adapter', 'number', [
      'string',
      'string',
      'string',
      'number',
      'number',
    ]),
    createFakeAdapter: wrap('encos_create_fake_adapter', 'number', [
      'string',
      'string',
      'number',
      'number',
    ]),
    disposeAdapter: wrap('encos_dispose_adapter', 'number', ['number']),
    adapterOk: wrap('encos_adapter_ok', 'number', ['number', 'number']),
    adapterGetBus: wrap('encos_adapter_get_bus', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]),
    adapterGetBusCount: wrap('encos_adapter_get_bus_count', 'number', [
      'number',
      'number',
    ]),
    adapterGetBusRawIndexAt: wrap('encos_adapter_get_bus_raw_index_at', 'number', [
      'number',
      'number',
      'number',
    ]),
    busGetIndex: wrap('encos_bus_get_index', 'number', ['number', 'number']),
    busGetMotorWithModel: wrap('encos_bus_get_motor_with_model', 'number', [
      'number',
      'number',
      'number',
      'number',
    ], { async: true }),
    busGetBattery: wrap('encos_bus_get_battery', 'number', [
      'number',
      'number',
      'number',
    ]),
    busGetImu: wrap('encos_bus_get_imu', 'number', [
      'number',
      'number',
      'number',
    ]),
    motorGetIndex: wrap('encos_motor_get_index', 'number', ['number', 'number']),
    batteryGetStatus: wrap('encos_battery_get_status', 'number', [
      'number',
      'number',
      'number',
      'number',
      'number',
    ]),
    batterySendPassiveCommands: wrap('encos_battery_send_passive_commands', 'number', [
      'number',
      'number',
      'number',
    ]),
    batteryClearFault: wrap('encos_battery_clear_fault', 'number', ['number']),
    batteryRequestCharging: wrap('encos_battery_request_charging', 'number', [
      'number',
      'number',
    ]),
    batteryAllowDischarge: wrap('encos_battery_allow_discharge', 'number', [
      'number',
      'number',
    ]),
    imuGetStatus: wrap('encos_imu_get_status', 'number', [
      'number',
      'number',
      'number',
      'number',
      'number',
    ]),
    motorGetStatus: wrap('encos_motor_get_status', 'number', [
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
    ]),
    motorPvtControl: wrap(
      'encos_motor_pvt_control',
      'number',
      [
        'number',
        'number',
        'number',
        'number',
        'number',
        'number',
        'number',
        'number',
        'number',
        'number',
        'number',
      ],
      { async: true },
    ),
    motorSpdControl: wrap(
      'encos_motor_spd_control',
      'number',
      ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'],
      { async: true },
    ),
    motorPosControl: wrap(
      'encos_motor_pos_control',
      'number',
      [
        'number',
        'number',
        'number',
        'number',
        'number',
        'number',
        'number',
        'number',
        'number',
      ],
      { async: true },
    ),
    motorCurControl: wrap(
      'encos_motor_cur_control',
      'number',
      ['number', 'number', 'number', 'number', 'number', 'number', 'number'],
      { async: true },
    ),
    motorSetPos: wrap(
      'encos_motor_set_pos',
      'number',
      ['number', 'number', 'number'],
      { async: true },
    ),
    motorResetZeroPos: wrap(
      'encos_motor_reset_zero_pos',
      'number',
      ['number', 'number', 'number'],
      { async: true },
    ),
    motorGetFloatParameter: wrap(
      'encos_motor_get_float_parameter',
      'number',
      ['number', 'number', 'number'],
      { async: true },
    ),
    motorSetCanTimeout: wrap(
      'encos_motor_set_can_timeout',
      'number',
      ['number', 'number', 'number', 'number'],
      { async: true },
    ),
    fakeSeedMotor: wrap('encos_fake_seed_motor', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]),
    fakeEnableAutoCreateMotor: wrap('encos_fake_enable_auto_create_motor', 'number', [
      'number',
    ]),
    fakeSetReplyMode: wrap('encos_fake_set_reply_mode', 'number', ['number', 'number']),
    fakeInjectFeedback: wrap('encos_fake_inject_feedback', 'number', [
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
    ]),
    fakeSetParameterWritePolicy: wrap(
      'encos_fake_set_parameter_write_policy',
      'number',
      ['number', 'number', 'number', 'number', 'number'],
    ),
    fakeInjectRawMessage: wrap('encos_fake_inject_raw_message', 'number', [
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
    ]),
    fakeGetMotorSnapshot: wrap('encos_fake_get_motor_snapshot', 'number', [
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
    ]),
    fakeGetCommandCount: wrap('encos_fake_get_command_count', 'number', [
      'number',
      'number',
    ]),
    fakeGetCommandKind: wrap('encos_fake_get_command_kind', 'number', [
      'number',
      'number',
      'number',
    ]),
    fakeGetCommandBusIndex: wrap('encos_fake_get_command_bus_index', 'number', [
      'number',
      'number',
      'number',
    ]),
    fakeGetCommandMotorIndex: wrap('encos_fake_get_command_motor_index', 'number', [
      'number',
      'number',
      'number',
    ]),
    fakeGetCommandNumberField: wrap('encos_fake_get_command_number_field', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]),
    fakeGetCommandIntField: wrap('encos_fake_get_command_int_field', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]),
    fakeGetRawMessageCount: wrap('encos_fake_get_raw_message_count', 'number', [
      'number',
      'number',
    ]),
    fakeGetRawMessageBusIndex: wrap('encos_fake_get_raw_message_bus_index', 'number', [
      'number',
      'number',
      'number',
    ]),
    fakeGetRawMessageCanId: wrap('encos_fake_get_raw_message_can_id', 'number', [
      'number',
      'number',
      'number',
    ]),
    fakeGetRawMessageFrameFlags: wrap(
      'encos_fake_get_raw_message_frame_flags',
      'number',
      ['number', 'number', 'number'],
    ),
    fakeGetRawMessageLen: wrap('encos_fake_get_raw_message_len', 'number', [
      'number',
      'number',
      'number',
    ]),
    fakeGetRawMessageDataByte: wrap('encos_fake_get_raw_message_data_byte', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]),
  }
}
