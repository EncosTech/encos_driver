// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

export type CFunction = (...args: any[]) => any

export interface WebSerialOpenOptions {
  baudRate: number
  dataBits?: 7 | 8
  stopBits?: 1 | 2
  parity?: 'none' | 'even' | 'odd'
  flowControl?: 'none' | 'hardware'
  bufferSize?: number
}

/** 浏览器 SerialPort 的结构类型，无需额外安装 Web Serial 类型包。 */
export interface WebSerialPort {
  readonly readable: ReadableStream<Uint8Array> | null
  readonly writable: WritableStream<Uint8Array> | null
  open(options: WebSerialOpenOptions): Promise<void>
  close(): Promise<void>
}

export interface WebSerialRequestOptions {
  filters?: Array<{ usbVendorId?: number; usbProductId?: number; bluetoothServiceClassId?: number | string }>
  allowedBluetoothServiceClassIds?: Array<number | string>
}

export interface WebSerialStatus {
  open: boolean
  attached?: boolean
  queuedBytes?: number
  error?: string | null
}

export interface WebSerialBridge {
  /** 在用户点击事件中请求权限并打开串口，返回 UsbSerial 的 interfaceName。 */
  requestPort(options?: WebSerialRequestOptions): Promise<string>
  /** 打开已经获得授权的浏览器串口。 */
  open(port: WebSerialPort): Promise<string>
  /** 等待取消读写、释放流锁并关闭串口。 */
  close(interfaceName: string): Promise<void>
  status(interfaceName: string): WebSerialStatus
}

export interface WasmModule {
  webSerial?: WebSerialBridge
  cwrap(
    ident: string,
    returnType: 'number',
    argTypes: string[],
    opts?: { async?: boolean },
  ): CFunction
  _malloc(size: number): number
  _free(ptr: number): void
  getValue(ptr: number, type: 'i8' | 'i32' | 'double'): number
  setValue(ptr: number, value: number, type: 'i8' | 'i32' | 'double'): void
  UTF8ToString(ptr: number): string
  _encos_get_last_error_message?: () => number
}

export type WasmModuleFactory = (options?: {
  locateFile?: (path: string) => string
  wasmBinary?: Uint8Array | ArrayBuffer
}) => Promise<WasmModule> | WasmModule

export interface RuntimeOptions {
  modulePath?: string
}

export interface CreateAdapterOptions<T extends string = string> {
  type: T
  interfaceName?: string
  loggerName?: string
  logLevel?: number
}

export interface CreateFakeAdapterOptions {
  interfaceName?: string
  loggerName?: string
  logLevel?: number
}

export interface BusKey {
  rawIndex: number
  slaveIndex: number | null
  busIndex: number
}

export interface ControlCommand {
  feedbackType?: number
}

export interface PVTControlCommand extends ControlCommand {
  kp: number
  kd: number
  position: number
  speed: number
  torque: number
}

export interface SpdControlCommand extends ControlCommand {
  speed: number
  current: number
}

export interface PosControlCommand extends ControlCommand {
  position: number
  speed: number
  current: number
}

export interface CurrentControlCommand extends ControlCommand {
  current: number
}

export interface FakeFeedbackInput {
  feedbackType?: number
  error?: number
  position?: number
  speed?: number
  current?: number
  motorTemperature?: number
  mosTemperature?: number
}

export interface BatteryPassiveCommands {
  allowShutdown?: boolean
  allowDischarge?: boolean
  parallelDischarge?: boolean
  forceShutdown?: boolean
  requestCharging?: boolean
  faultShutdownBroadcast?: boolean
  configureFaultThresholds?: boolean
  clearFault?: boolean
  factoryMode?: boolean
  debug?: boolean
}

export interface BatteryStatusResult {
  state?: {
    isMaster: boolean
    soc: number
    voltage: number
    allowedDischargeCurrent: number
    allowedChargeCurrent: number
  }
  temp?: {
    battery: number
    mos: number
    dischargeCurrent: number
    chargeCurrent: number
  }
  error: {
    couldNotCharge: boolean
    couldNotDischarge: boolean
    lowBattery: boolean
    overCurrentSteady: boolean
    overCurrentPeak: boolean
    overCurrentCharge: boolean
    batteryOverTemp: boolean
    mosOverTemp: boolean
    couldNotCommunicate: boolean
    stoppedEmergency: boolean
    chargerFault: boolean
    commTimeout: boolean
  }
  activeCommands?: {
    shutdownRequest: boolean
    dischargeRequest: boolean
    forceShutdownBroadcast: boolean
    allowCharging: boolean
    faultShutdownBroadcast: boolean
    mosStatus: boolean
  }
}

export interface ImuStatusResult {
  acceleration?: {
    x: number
    y: number
    z: number
  }
  angularVelocity?: {
    x: number
    y: number
    z: number
  }
  eulerAngle?: {
    pitch: number
    roll: number
    heading: number
  }
  quaternion?: {
    qw: number
    qx: number
    qy: number
    qz: number
  }
}

export interface ControlResult {
  ok: boolean
  feedbackType: number
  hasFeedback: boolean
  noResponse: boolean
  feedback?: {
    error: number
    position: number
    speed: number
    current: number
    motorTemperature: number
    mosTemperature: number
  }
}

export interface StatusResult {
  ok: boolean
  hasValue: boolean
  status?: {
    error: number
    position: number
    speed: number
    current: number
    motorTemperature: number
    mosTemperature: number
  }
}

export interface FakeCommandRecord {
  kind: number
  busIndex: number
  motorIndex: number
  payload: Record<string, number>
}

export interface FakeRawMessageRecord {
  busIndex: number
  canId: number
  frameFlags: number
  data: number[]
}
