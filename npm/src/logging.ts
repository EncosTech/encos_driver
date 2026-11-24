// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import type { Runtime } from './runtime.js'
import { LogLevel } from './constants.js'
export type LogArgument = string | number | boolean | bigint
export type LogField = LogArgument | null | undefined

function streamArgument(value: LogArgument): string {
  if (typeof value === 'boolean') return value ? '1' : '0'
  if (typeof value !== 'number') return String(value)
  if (Number.isNaN(value)) return 'nan'
  if (!Number.isFinite(value)) return value < 0 ? '-inf' : 'inf'
  if (Object.is(value, -0)) return '-0'
  if (value === 0) return '0'
  const rounded = Number(value.toPrecision(6))
  const exponent = Math.floor(Math.log10(Math.abs(rounded)))
  if (exponent < -4 || exponent >= 6) {
    const [coefficient, power] = rounded.toExponential(5).split('e')
    return coefficient.replace(/\.?0+$/, '') + 'e' + (Number(power) < 0 ? '-' : '+') + Math.abs(Number(power)).toString().padStart(2, '0')
  }
  return rounded.toFixed(Math.max(0, 5 - exponent)).replace(/(\.\d*?)0+$/, '$1').replace(/\.$/, '')
}

/** 与 C++ Logger::FormatInto 相同：替换下一对大括号，剩余参数以空格追加。 */
function formatMessage(message: string, args: LogArgument[]): string {
  let result = ''
  let start = 0
  for (let i = 0; i < args.length; i++) {
    const open = message.indexOf('{', start)
    const close = open === -1 ? -1 : message.indexOf('}', open)
    if (close === -1) return result + message.slice(start) + ' ' + args.slice(i).map(streamArgument).join(' ')
    result += message.slice(start, open) + streamArgument(args[i])
    start = close + 1
  }
  return result + message.slice(start)
}

export class Logger {
  disposed = false
  constructor(readonly runtime: Runtime, readonly handle: number) {}
  setLevel(level: number): void { this.#invoke('loggerSetLevel', { level }) }
  level(): number { return this.#invoke('loggerLevel') }
  name(): string { return this.#invoke('loggerName') }
  flush(): void { this.#invoke('loggerFlush') }
  trace(message: string, ...args: LogArgument[]): void { this.#log(LogLevel.Trace, message, args) }
  debug(message: string, ...args: LogArgument[]): void { this.#log(LogLevel.Debug, message, args) }
  info(message: string, ...args: LogArgument[]): void { this.#log(LogLevel.Info, message, args) }
  warn(message: string, ...args: LogArgument[]): void { this.#log(LogLevel.Warn, message, args) }
  error(message: string, ...args: LogArgument[]): void { this.#log(LogLevel.Error, message, args) }
  critical(message: string, ...args: LogArgument[]): void { this.#log(LogLevel.Critical, message, args) }
  dispose(): void { if (!this.disposed) { this.#invoke('loggerDispose'); this.disposed = true } }
  #log(level: number, message: string, args: LogArgument[]): void {
    this.#invoke('loggerLog', { level, message: formatMessage(message, args) })
  }
  #invoke<T>(name: string, args: Record<string, unknown> = {}): T {
    if (this.disposed) throw new Error('logger is disposed')
    return this.runtime._invoke(`utility.${name}`, { handle: this.handle, ...args })
  }
}

export class LogWriter {
  disposed = false
  constructor(readonly runtime: Runtime, readonly handle: number) {}
  write(...fields: LogField[]): void { this.#invoke('writerWrite', { fields: fields.map(field => field ?? null) }) }
  flush(): void { this.#invoke('writerFlush') }
  getBaseName(): string { return this.#invoke('writerBaseName') }
  getFileName(): string { return this.#invoke('writerFileName') }
  dispose(): void { if (!this.disposed) { this.#invoke('writerDispose'); this.disposed = true } }
  #invoke<T>(name: string, args: Record<string, unknown> = {}): T {
    if (this.disposed) throw new Error('log writer is disposed')
    return this.runtime._invoke(`utility.${name}`, { handle: this.handle, ...args })
  }
}
