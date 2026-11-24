// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import { describe, expect, it } from 'vitest'
import {
  AdapterWrapper,
  StatisticsStatus,
  ErrorCounterKind,
  type AdapterStatistics
} from '../../npm/src/adapter.js'
import { Runtime } from '../../npm/src/runtime.js'
import type { RuntimeBackend } from '../../npm/src/backend.js'

function adapter(backend: RuntimeBackend): AdapterWrapper {
  return new AdapterWrapper(new Runtime(backend, {
    resolveUrl: input => new URL(input),
    fetch: input => globalThis.fetch(input),
  }), 7, 'Fake', 'statistics-test')
}

describe('adapter common statistics', () => {
  it('reports unsupported for WASM without invoking native-only operations', () => {
    expect(adapter({ kind: 'wasm', calls: {}, lastError: '' }).getStatistics()).toBeNull()
  })

  it('preserves required addresses, enum values, zero counters and absent samples', () => {
    const statistics: AdapterStatistics = {
      buses: [
        {
          slaveId: 0,
          busId: 0,
          status: StatisticsStatus.Valid,
          ageMs: 0,
          errorCounters: {
            tx: 0,
            rx: 0,
            kind: ErrorCounterKind.Current
          }
        },
        { slaveId: 2, busId: 7, status: StatisticsStatus.Pending },
        {
          slaveId: 0,
          busId: 1,
          status: StatisticsStatus.Stale,
          errorCounters: {
            tx: 12,
            rx: 37,
            kind: ErrorCounterKind.LastErrorEvent
          }
        },
      ]
    }

    const instance = adapter({
      kind: 'native',
      calls: {},
      lastError: '',

      invoke: <T>(name: string, args: Record<string, unknown>): T => {
        expect(name).toBe('adapter.getStatistics')
        expect(args.handle).toBe(7)

        return statistics as T
      },
    })

    expect(instance.getStatistics()).toEqual(statistics)
    expect(instance.getStatistics()?.buses[1].errorCounters).toBeUndefined()
    expect(Object.values(StatisticsStatus)).toEqual(['pending', 'valid', 'stale'])
    expect(Object.values(ErrorCounterKind)).toEqual(['current', 'last-error-event'])
  })
})
