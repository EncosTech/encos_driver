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
  return {
    createAdapter: module.cwrap('encos_create_adapter', 'number', [
      'string',
      'string',
      'string',
      'number',
      'number',
    ]),
    createFakeAdapter: module.cwrap('encos_create_fake_adapter', 'number', [
      'string',
      'string',
      'number',
      'number',
    ]),
    disposeAdapter: module.cwrap('encos_dispose_adapter', 'number', ['number']),
    adapterOk: module.cwrap('encos_adapter_ok', 'number', ['number', 'number']),
    adapterGetBus: module.cwrap('encos_adapter_get_bus', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]),
    adapterGetBusCount: module.cwrap('encos_adapter_get_bus_count', 'number', [
      'number',
      'number',
    ]),
    adapterGetBusRawIndexAt: module.cwrap('encos_adapter_get_bus_raw_index_at', 'number', [
      'number',
      'number',
      'number',
    ]),
    busGetIndex: module.cwrap('encos_bus_get_index', 'number', ['number', 'number']),
    busGetMotorWithModel: module.cwrap('encos_bus_get_motor_with_model', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]),
    busGetBattery: module.cwrap('encos_bus_get_battery', 'number', [
      'number',
      'number',
      'number',
    ]),
    busGetImu: module.cwrap('encos_bus_get_imu', 'number', [
      'number',
      'number',
      'number',
    ]),
    motorGetIndex: module.cwrap('encos_motor_get_index', 'number', ['number', 'number']),
    batteryGetStatus: module.cwrap('encos_battery_get_status', 'number', [
      'number',
      'number',
      'number',
      'number',
      'number',
    ]),
    batterySendPassiveCommands: module.cwrap('encos_battery_send_passive_commands', 'number', [
      'number',
      'number',
      'number',
    ]),
    batteryClearFault: module.cwrap('encos_battery_clear_fault', 'number', ['number']),
    batteryRequestCharging: module.cwrap('encos_battery_request_charging', 'number', [
      'number',
      'number',
    ]),
    batteryAllowDischarge: module.cwrap('encos_battery_allow_discharge', 'number', [
      'number',
      'number',
    ]),
    imuGetStatus: module.cwrap('encos_imu_get_status', 'number', [
      'number',
      'number',
      'number',
      'number',
      'number',
    ]),
    motorGetStatus: module.cwrap('encos_motor_get_status', 'number', [
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
    ]),
    motorPvtControl: module.cwrap(
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
    motorSpdControl: module.cwrap(
      'encos_motor_spd_control',
      'number',
      ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'],
      { async: true },
    ),
    motorPosControl: module.cwrap(
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
    motorCurControl: module.cwrap(
      'encos_motor_cur_control',
      'number',
      ['number', 'number', 'number', 'number', 'number', 'number', 'number'],
      { async: true },
    ),
    motorSetPos: module.cwrap(
      'encos_motor_set_pos',
      'number',
      ['number', 'number', 'number'],
      { async: true },
    ),
    motorResetZeroPos: module.cwrap(
      'encos_motor_reset_zero_pos',
      'number',
      ['number', 'number', 'number'],
      { async: true },
    ),
    motorGetFloatParameter: module.cwrap(
      'encos_motor_get_float_parameter',
      'number',
      ['number', 'number', 'number'],
      { async: true },
    ),
    motorSetCanTimeout: module.cwrap(
      'encos_motor_set_can_timeout',
      'number',
      ['number', 'number', 'number', 'number'],
      { async: true },
    ),
    fakeSeedMotor: module.cwrap('encos_fake_seed_motor', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]),
    fakeEnableAutoCreateMotor: module.cwrap('encos_fake_enable_auto_create_motor', 'number', [
      'number',
    ]),
    fakeSetReplyMode: module.cwrap('encos_fake_set_reply_mode', 'number', ['number', 'number']),
    fakeInjectFeedback: module.cwrap('encos_fake_inject_feedback', 'number', [
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
    ]),
    fakeSetParameterWritePolicy: module.cwrap(
      'encos_fake_set_parameter_write_policy',
      'number',
      ['number', 'number', 'number', 'number', 'number'],
    ),
    fakeInjectRawMessage: module.cwrap('encos_fake_inject_raw_message', 'number', [
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
    ]),
    fakeGetMotorSnapshot: module.cwrap('encos_fake_get_motor_snapshot', 'number', [
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
      'number',
    ]),
    fakeGetCommandCount: module.cwrap('encos_fake_get_command_count', 'number', [
      'number',
      'number',
    ]),
    fakeGetCommandKind: module.cwrap('encos_fake_get_command_kind', 'number', [
      'number',
      'number',
      'number',
    ]),
    fakeGetCommandBusIndex: module.cwrap('encos_fake_get_command_bus_index', 'number', [
      'number',
      'number',
      'number',
    ]),
    fakeGetCommandMotorIndex: module.cwrap('encos_fake_get_command_motor_index', 'number', [
      'number',
      'number',
      'number',
    ]),
    fakeGetCommandNumberField: module.cwrap('encos_fake_get_command_number_field', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]),
    fakeGetCommandIntField: module.cwrap('encos_fake_get_command_int_field', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]),
    fakeGetRawMessageCount: module.cwrap('encos_fake_get_raw_message_count', 'number', [
      'number',
      'number',
    ]),
    fakeGetRawMessageBusIndex: module.cwrap('encos_fake_get_raw_message_bus_index', 'number', [
      'number',
      'number',
      'number',
    ]),
    fakeGetRawMessageCanId: module.cwrap('encos_fake_get_raw_message_can_id', 'number', [
      'number',
      'number',
      'number',
    ]),
    fakeGetRawMessageFrameFlags: module.cwrap(
      'encos_fake_get_raw_message_frame_flags',
      'number',
      ['number', 'number', 'number'],
    ),
    fakeGetRawMessageLen: module.cwrap('encos_fake_get_raw_message_len', 'number', [
      'number',
      'number',
      'number',
    ]),
    fakeGetRawMessageDataByte: module.cwrap('encos_fake_get_raw_message_data_byte', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]),
  }
}
