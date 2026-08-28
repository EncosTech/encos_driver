import { FakeCommandKind } from './constants.js'
import { allocU32, assertOk, getI32 } from './native.js'
import type {
  CFunction,
  FakeCommandRecord,
  FakeFeedbackInput,
  FakeRawMessageRecord,
} from './types.js'
import type { FakeAdapterWrapper } from './adapter.js'

export class FakeTools {
  adapter: FakeAdapterWrapper
  runtime: FakeAdapterWrapper['runtime']

  constructor(adapter: FakeAdapterWrapper) {
    this.adapter = adapter
    this.runtime = adapter.runtime
  }

  seedMotor(busIndex: number, motorIndex: number, model: number): void {
    this.adapter._assertLiveForChild()
    const code = this.runtime.calls.fakeSeedMotor(this.adapter.handle, busIndex, motorIndex, model)
    assertOk(this.runtime, code, 'failed to seed fake motor')
  }

  enableAutoCreateMotor(): void {
    this.adapter._assertLiveForChild()
    const code = this.runtime.calls.fakeEnableAutoCreateMotor(this.adapter.handle)
    assertOk(this.runtime, code, 'failed to enable auto-create motor')
  }

  setReplyMode(mode: number): void {
    this.adapter._assertLiveForChild()
    const code = this.runtime.calls.fakeSetReplyMode(this.adapter.handle, mode)
    assertOk(this.runtime, code, 'failed to set reply mode')
  }

  injectFeedback(busIndex: number, motorIndex: number, status: FakeFeedbackInput): void {
    this.adapter._assertLiveForChild()
    const values = this.runtime.module._malloc(5 * 8)
    const meta = this.runtime.module._malloc(4)
    try {
      this.runtime.module.setValue(values, status.position ?? Number.NaN, 'double')
      this.runtime.module.setValue(values + 8, status.speed ?? Number.NaN, 'double')
      this.runtime.module.setValue(values + 16, status.current ?? Number.NaN, 'double')
      this.runtime.module.setValue(values + 24, status.motorTemperature ?? Number.NaN, 'double')
      this.runtime.module.setValue(values + 32, status.mosTemperature ?? Number.NaN, 'double')
      this.runtime.module.setValue(meta, status.error ?? 0, 'i32')
      const code = this.runtime.calls.fakeInjectFeedback(
        this.adapter.handle,
        busIndex,
        motorIndex,
        status.feedbackType ?? 1,
        values,
        5,
        meta,
        1,
      )
      assertOk(this.runtime, code, 'failed to inject fake feedback')
    } finally {
      this.runtime.module._free(values)
      this.runtime.module._free(meta)
    }
  }

  setParameterWritePolicy(busIndex: number, motorIndex: number, parameter: number, policy: number): void {
    this.adapter._assertLiveForChild()
    const code = this.runtime.calls.fakeSetParameterWritePolicy(
      this.adapter.handle,
      busIndex,
      motorIndex,
      parameter,
      policy,
    )
    assertOk(this.runtime, code, 'failed to set parameter write policy')
  }

  injectRawMessage(busIndex: number, canId: number, data: number[], frameFlags = 0): void {
    this.adapter._assertLiveForChild()
    const payload = this.runtime.module._malloc(data.length)
    try {
      for (let i = 0; i < data.length; i += 1) {
        this.runtime.module.setValue(payload + i, data[i] ?? 0, 'i8')
      }
      const code = this.runtime.calls.fakeInjectRawMessage(
        this.adapter.handle,
        busIndex,
        canId,
        frameFlags,
        payload,
        data.length,
      )
      assertOk(this.runtime, code, 'failed to inject fake raw message')
    } finally {
      this.runtime.module._free(payload)
    }
  }

  getMotorSnapshot(busIndex: number, motorIndex: number) {
    this.adapter._assertLiveForChild()
    const values = this.runtime.module._malloc(21 * 8)
    const meta = this.runtime.module._malloc(6 * 4)
    try {
      const code = this.runtime.calls.fakeGetMotorSnapshot(
        this.adapter.handle,
        busIndex,
        motorIndex,
        values,
        21,
        meta,
        6,
      )
      assertOk(this.runtime, code, 'failed to read fake motor snapshot')
      return {
        model: this.runtime.module.getValue(meta, 'i32'),
        ranges: {
          kp: { min: this.#doubleAt(values, 0), max: this.#doubleAt(values, 1) },
          kd: { min: this.#doubleAt(values, 2), max: this.#doubleAt(values, 3) },
          position: { min: this.#doubleAt(values, 4), max: this.#doubleAt(values, 5) },
          speed: { min: this.#doubleAt(values, 6), max: this.#doubleAt(values, 7) },
          torque: { min: this.#doubleAt(values, 8), max: this.#doubleAt(values, 9) },
          current: { min: this.#doubleAt(values, 10), max: this.#doubleAt(values, 11) },
          kt: this.#doubleAt(values, 12),
        },
        positionRad: this.#doubleAt(values, 13),
        speedRadS: this.#doubleAt(values, 14),
        currentA: this.#doubleAt(values, 15),
        torqueNm: this.#doubleAt(values, 16),
        motorTempC: this.#doubleAt(values, 17),
        mosTempC: this.#doubleAt(values, 18),
        acceleration: this.#doubleAt(values, 19),
        kt: this.#doubleAt(values, 20),
        canTimeoutMs: this.runtime.module.getValue(meta + 4, 'i32'),
        replyFrameFlags: this.runtime.module.getValue(meta + 8, 'i32'),
        communicationMode: this.runtime.module.getValue(meta + 12, 'i32'),
        error: this.runtime.module.getValue(meta + 16, 'i32'),
        brakeEnabled: this.runtime.module.getValue(meta + 20, 'i32') !== 0,
      }
    } finally {
      this.runtime.module._free(values)
      this.runtime.module._free(meta)
    }
  }

  getCommandRecords(): FakeCommandRecord[] {
    this.adapter._assertLiveForChild()
    const out = allocU32(this.runtime.module)
    try {
      const code = this.runtime.calls.fakeGetCommandCount(this.adapter.handle, out)
      assertOk(this.runtime, code, 'failed to read fake commands')
      return Array.from({ length: getI32(this.runtime.module, out) }, (_, index) => this.#commandAt(index))
    } finally {
      this.runtime.module._free(out)
    }
  }

  getLastCommand(): FakeCommandRecord | null {
    const records = this.getCommandRecords()
    return records.length === 0 ? null : records[records.length - 1]
  }

  getRawMessageRecords(): FakeRawMessageRecord[] {
    this.adapter._assertLiveForChild()
    const out = allocU32(this.runtime.module)
    try {
      const code = this.runtime.calls.fakeGetRawMessageCount(this.adapter.handle, out)
      assertOk(this.runtime, code, 'failed to read fake raw messages')
      return Array.from({ length: getI32(this.runtime.module, out) }, (_, index) =>
        this.#rawMessageAt(index),
      )
    } finally {
      this.runtime.module._free(out)
    }
  }

  getLastRawCommand(): FakeRawMessageRecord | null {
    const records = this.getRawMessageRecords()
    return records.length === 0 ? null : records[records.length - 1]
  }

  #commandAt(index: number): FakeCommandRecord {
    return {
      kind: this.#intCommandField(this.runtime.calls.fakeGetCommandKind, index),
      busIndex: this.#intCommandField(this.runtime.calls.fakeGetCommandBusIndex, index),
      motorIndex: this.#intCommandField(this.runtime.calls.fakeGetCommandMotorIndex, index),
      payload: this.#payloadAt(index),
    }
  }

  #payloadAt(index: number): Record<string, number> {
    const kind = this.#intCommandField(this.runtime.calls.fakeGetCommandKind, index)
    if (kind === FakeCommandKind.PVTControl) {
      return {
        kp: this.#numberField(index, 1),
        kd: this.#numberField(index, 2),
        position: this.#numberField(index, 3),
        speed: this.#numberField(index, 4),
        torque: this.#numberField(index, 5),
      }
    }
    if (kind === FakeCommandKind.SpdControl) {
      return {
        speed: this.#numberField(index, 1),
        current: this.#numberField(index, 2),
        feedbackType: this.#intField(index, 1),
      }
    }
    if (kind === FakeCommandKind.PosControl) {
      return {
        position: this.#numberField(index, 3),
        speed: this.#numberField(index, 1),
        current: this.#numberField(index, 2),
        feedbackType: this.#intField(index, 1),
      }
    }
    if (kind === FakeCommandKind.CurControl) {
      return {
        current: this.#numberField(index, 1),
        feedbackType: this.#intField(index, 1),
      }
    }
    if (kind === FakeCommandKind.SetPos) {
      return {
        position: this.#numberField(index, 1),
      }
    }
    if (kind === FakeCommandKind.ResetZeroPos) {
      return {}
    }
    if (kind === FakeCommandKind.SetParameter) {
      return {
        parameter: this.#intField(index, 2),
      }
    }
    return {}
  }

  #rawMessageAt(index: number): FakeRawMessageRecord {
    const len = this.#intRawField(this.runtime.calls.fakeGetRawMessageLen, index)
    return {
      busIndex: this.#intRawField(this.runtime.calls.fakeGetRawMessageBusIndex, index),
      canId: this.#intRawField(this.runtime.calls.fakeGetRawMessageCanId, index),
      frameFlags: this.#intRawField(this.runtime.calls.fakeGetRawMessageFrameFlags, index),
      data: Array.from({ length: len }, (_, byteIndex) => this.#rawByte(index, byteIndex)),
    }
  }

  #intCommandField(fn: CFunction, index: number): number {
    const out = allocU32(this.runtime.module)
    try {
      const code = fn(this.adapter.handle, index, out)
      assertOk(this.runtime, code, 'failed to read fake command')
      return getI32(this.runtime.module, out)
    } finally {
      this.runtime.module._free(out)
    }
  }

  #numberField(index: number, fieldId: number): number {
    const out = this.runtime.module._malloc(8)
    try {
      const code = this.runtime.calls.fakeGetCommandNumberField(
        this.adapter.handle,
        index,
        fieldId,
        out,
      )
      assertOk(this.runtime, code, 'failed to read fake command number field')
      return this.runtime.module.getValue(out, 'double')
    } finally {
      this.runtime.module._free(out)
    }
  }

  #intField(index: number, fieldId: number): number {
    const out = allocU32(this.runtime.module)
    try {
      const code = this.runtime.calls.fakeGetCommandIntField(
        this.adapter.handle,
        index,
        fieldId,
        out,
      )
      assertOk(this.runtime, code, 'failed to read fake command int field')
      return getI32(this.runtime.module, out)
    } finally {
      this.runtime.module._free(out)
    }
  }

  #intRawField(fn: CFunction, index: number): number {
    const out = allocU32(this.runtime.module)
    try {
      const code = fn(this.adapter.handle, index, out)
      assertOk(this.runtime, code, 'failed to read fake raw message field')
      return getI32(this.runtime.module, out)
    } finally {
      this.runtime.module._free(out)
    }
  }

  #rawByte(index: number, byteIndex: number): number {
    const out = allocU32(this.runtime.module)
    try {
      const code = this.runtime.calls.fakeGetRawMessageDataByte(
        this.adapter.handle,
        index,
        byteIndex,
        out,
      )
      assertOk(this.runtime, code, 'failed to read fake raw message byte')
      return getI32(this.runtime.module, out)
    } finally {
      this.runtime.module._free(out)
    }
  }

  #doubleAt(ptr: number, index: number): number {
    return this.runtime.module.getValue(ptr + index * 8, 'double')
  }
}
