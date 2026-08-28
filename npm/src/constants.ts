export const LogLevel = Object.freeze({
  Trace: 0,
  Debug: 1,
  Info: 2,
  Warn: 3,
  Error: 4,
  Critical: 5,
  Off: 6,
})

export const MotorModel = Object.freeze({
  EC_A2806_P2: 0,
  EC_A2806_P2_72V: 1,
  EC_A4310_P2: 2,
  EC_A4310_P2_72V: 3,
  EC_A4315_P2: 4,
  EC_A4315_P2_72V: 5,
  EC_A10010_P2: 36,
})

export const MotorParameter = Object.freeze({
  Position: 1,
  Speed: 2,
  Current: 3,
  CanTimeout: 31,
  BrakeStatus: 37,
})

export const FakeReplyMode = Object.freeze({
  Automatic: 0,
  Manual: 1,
})

export const FakeWritePolicy = Object.freeze({
  Success: 0,
  Failure: 1,
  Ignore: 2,
})

export const FakeCommandKind = Object.freeze({
  Unknown: 0,
  PVTControl: 1,
  PosControl: 2,
  SpdControl: 3,
  CurControl: 4,
  TorControl: 5,
  Stop: 6,
  Brake: 7,
  SetId: 8,
  SetPos: 9,
  ResetZeroPos: 10,
  GetParameter: 11,
  SetParameter: 12,
})
