// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import { NumericBuffer } from './backend.js'
import type { FakeMotorSnapshot, FakeSeedOptions, FakeReplyRecord, DecodedFakeCommandRecord, MotorMessage, MotorStatus, StatusCallback } from './native-types.js'
import { FakeCommandKind } from './constants.js'
import { allocU32, assertOk, getI32 } from './native.js'
import type {
  CFunction,
  FakeCommandRecord,
  FakeFeedbackInput,
  FakeRawMessageRecord,
} from './types.js'
import type { AdapterWrapper } from './adapter.js'

export class FakeTools {
  adapter: AdapterWrapper
  runtime: AdapterWrapper['runtime']

  constructor(adapter: AdapterWrapper) {
    this.adapter = adapter
    this.runtime = adapter.runtime
  }

  seedMotor(busIndex: number, motorIndex: number, modelOrSnapshot: number | FakeMotorSnapshot, options: FakeSeedOptions = {}): void {
    this.adapter._assertLiveForChild()
    if (this.runtime.backend.kind === 'native') {
      this.#invoke('seedMotor', { busIndex, motorIndex, ...(typeof modelOrSnapshot === 'number' ? { model: modelOrSnapshot, options } : { snapshot: modelOrSnapshot }) })
      return
    }
    if (typeof modelOrSnapshot !== 'number' || options.replyFrameFlags !== undefined) throw new Error('snapshot seeding is only supported by the native runtime')
    const model = modelOrSnapshot
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
    const values = new NumericBuffer('double', 5)
    const meta = new NumericBuffer('i32', 1)
    try {
      values.set(0, status.position ?? Number.NaN)
      values.set(1, status.speed ?? Number.NaN)
      values.set(2, status.current ?? Number.NaN)
      values.set(3, status.motorTemperature ?? Number.NaN)
      values.set(4, status.mosTemperature ?? Number.NaN)
      meta.set(0, status.error ?? 0)
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
      values.release()
      meta.release()
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
    const payload = new NumericBuffer('i8', data.length)
    try {
      for (let i = 0; i < data.length; i += 1) {
        payload.set(i, data[i] ?? 0)
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
      payload.release()
    }
  }

  getMotorSnapshot(busIndex: number, motorIndex: number): FakeMotorSnapshot {
    this.adapter._assertLiveForChild()
    if (this.runtime.backend.kind === 'native') return this.#invoke('getMotorSnapshot', { busIndex, motorIndex })
    const values = new NumericBuffer('double', 21)
    const meta = new NumericBuffer('i32', 6)
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
        model: meta.get(0),
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
        canTimeoutMs: meta.get(1),
        replyFrameFlags: meta.get(2),
        communicationMode: meta.get(3),
        error: meta.get(4),
        brakeEnabled: meta.get(5) !== 0,
      } as FakeMotorSnapshot
    } finally {
      values.release()
      meta.release()
    }
  }

  getCommandRecords(): FakeCommandRecord[] {
    this.adapter._assertLiveForChild()
    const out = allocU32()
    try {
      const code = this.runtime.calls.fakeGetCommandCount(this.adapter.handle, out)
      assertOk(this.runtime, code, 'failed to read fake commands')
      return Array.from({ length: getI32(out) }, (_, index) => this.#commandAt(index))
    } finally {
      out.release()
    }
  }

  getLastCommand(): FakeCommandRecord | null {
    const records = this.getCommandRecords()
    return records.length === 0 ? null : records[records.length - 1]
  }

  getRawMessageRecords(): FakeRawMessageRecord[] {
    this.adapter._assertLiveForChild()
    const out = allocU32()
    try {
      const code = this.runtime.calls.fakeGetRawMessageCount(this.adapter.handle, out)
      assertOk(this.runtime, code, 'failed to read fake raw messages')
      return Array.from({ length: getI32(out) }, (_, index) =>
        this.#rawMessageAt(index),
      )
    } finally {
      out.release()
    }
  }

  getLastRawCommand(): FakeRawMessageRecord | null {
    const records = this.getRawMessageRecords()
    return records.length === 0 ? null : records[records.length - 1]
  }

  createSnapshot(model: number): FakeMotorSnapshot { return this.#invoke('createSnapshot', { model }) }
  disableAutoCreateMotor(): void { this.#invoke('disableAutoCreateMotor') }
  enableCommandRecording(enabled = true): void { this.#invoke('enableCommandRecording', { enabled }) }
  enablePositionError(enabled = true): void { this.#invoke('enablePositionError', { enabled }) }
  setDecodedCommandObserver(callback: StatusCallback<DecodedFakeCommandRecord>): void {
    this.adapter._assertLiveForChild()
    this.runtime._subscribe('fake.decodedCommand', { handle: this.adapter.handle }, callback)
  }
  clearDecodedCommandObserver(): void { this.setDecodedCommandObserver(null) }
  getDecodedCommandRecords(): DecodedFakeCommandRecord[] { return this.#invoke('getDecodedCommandRecords') }
  getLastDecodedCommand(): DecodedFakeCommandRecord | null { return this.getDecodedCommandRecords().at(-1) ?? null }
  getFormattedSentCommands(): string[] { return this.#invoke('getFormattedSentCommands') }
  getReplyRecords(): FakeReplyRecord[] { return this.#invoke('getReplyRecords') }
  getRawSentMessages(): MotorMessage[] { return this.#invoke('getRawSentMessages') }
  clearCommandRecords(): void { this.#invoke('clearCommandRecords') }
  injectMessage(message: MotorMessage): void { this.#invoke('injectMessage', { message }) }
  makeFeedbackMessage(busIndex: number, motorIndex: number, status: MotorStatus, feedbackType = 1): MotorMessage {
    return this.#invoke('makeFeedbackMessage', { busIndex, motorIndex, status, feedbackType })
  }
  makeParameterReply(busIndex: number, motorIndex: number, parameter: number, payload: number[], frameFlags = 0): MotorMessage {
    return this.#invoke('makeParameterReply', { busIndex, motorIndex, parameter, payload, frameFlags })
  }
  makeWriteAck(busIndex: number, motorIndex: number, parameter: number, payload: number[], frameFlags = 0): MotorMessage {
    return this.#invoke('makeWriteAck', { busIndex, motorIndex, parameter, payload, frameFlags })
  }
  #invoke<T>(name: string, args: Record<string, unknown> = {}): T {
    this.adapter._assertLiveForChild()
    return this.runtime._invoke(`fake.${name}`, { handle: this.adapter.handle, ...args })
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
    const out = allocU32()
    try {
      const code = fn(this.adapter.handle, index, out)
      assertOk(this.runtime, code, 'failed to read fake command')
      return getI32(out)
    } finally {
      out.release()
    }
  }

  #numberField(index: number, fieldId: number): number {
    const out = new NumericBuffer('double', 1, false)
    try {
      const code = this.runtime.calls.fakeGetCommandNumberField(
        this.adapter.handle,
        index,
        fieldId,
        out,
      )
      assertOk(this.runtime, code, 'failed to read fake command number field')
      return out.get(0)
    } finally {
      out.release()
    }
  }

  #intField(index: number, fieldId: number): number {
    const out = allocU32()
    try {
      const code = this.runtime.calls.fakeGetCommandIntField(
        this.adapter.handle,
        index,
        fieldId,
        out,
      )
      assertOk(this.runtime, code, 'failed to read fake command int field')
      return getI32(out)
    } finally {
      out.release()
    }
  }

  #intRawField(fn: CFunction, index: number): number {
    const out = allocU32()
    try {
      const code = fn(this.adapter.handle, index, out)
      assertOk(this.runtime, code, 'failed to read fake raw message field')
      return getI32(out)
    } finally {
      out.release()
    }
  }

  #rawByte(index: number, byteIndex: number): number {
    const out = allocU32()
    try {
      const code = this.runtime.calls.fakeGetRawMessageDataByte(
        this.adapter.handle,
        index,
        byteIndex,
        out,
      )
      assertOk(this.runtime, code, 'failed to read fake raw message byte')
      return getI32(out)
    } finally {
      out.release()
    }
  }

  #doubleAt(ptr: NumericBuffer, index: number): number {
    return ptr.get(index)
  }
}
