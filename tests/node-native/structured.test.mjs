// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { resolve } from 'node:path';
import test from 'node:test';
const require = createRequire(import.meta.url);
const root = process.env.ENCOS_NODE_PACKAGE_ROOT;
const addonPath = process.env.ENCOS_NODE_ADDON ?? (root && resolve(root, 'native/encosdriver.node'));
const addon = addonPath ? require(addonPath) : null;
const create = () => addon.createRuntime(...(root ? [resolve(root, 'runtime/lib/encosPlugins')] : []));
const value = result => { assert.equal(result.code, 0, result.message); return result.value; };
const scalar = result => { assert.equal(result.code, 0, result.message); return result.outputs[0][0]; };
const tick = () => new Promise(resolveTick => setImmediate(resolveTick));

test('structured integer results retain 64-bit precision and reject fractional inputs', { skip: !addon }, () => {
    const runtime = create();
    try {
        assert.equal(value(runtime.invoke('manager.makeReceiveUniqueId', { busIndex: -1, canId: 0xffffffff })), 0xffffffffffffffffn);
        assert.equal(value(runtime.invoke('manager.makeReceiveUniqueId', { busIndex: 0, canId: 42 })), 42);
        assert.notEqual(runtime.invoke('manager.makeReceiveUniqueId', { busIndex: 0.5, canId: 1 }).code, 0);
        assert.throws(() => runtime.invoke('runtime.getVersion', { cyclic: (() => { const x = {}; x.self = x; return x; })() }), /nesting/);
        assert.throws(() => runtime.invoke('runtime.getVersion', { bigint: 1n << 100n }), /64-bit/);
    } finally { runtime.dispose(); }
});

test('callback replacement and native identity deletion invalidate queued events across aliases', { skip: !addon }, async () => {
    const runtime = create();
    try {
        const name = `structured-callback-${process.pid}`;
        const first = scalar(runtime.call('encos_create_fake_adapter', [name, '', 3]));
        const second = scalar(runtime.call('encos_create_fake_adapter', [name, '', 3]));
        assert.equal(runtime.call('encos_fake_seed_motor', [first, 0, 1, 0]).code, 0);
        const bus1 = value(runtime.invoke('adapter.getBus', { handle: first, busIndex: 0, slaveIndex: undefined }));
        const bus2 = value(runtime.invoke('adapter.getBus', { handle: second, busIndex: 0 }));
        const motor1 = value(await runtime.invokeAsync('bus.getMotor', { handle: bus1, index: 1, model: 0, canfd: undefined }));
        const motor2 = value(await runtime.invokeAsync('bus.getMotor', { handle: bus2, index: 1, model: 0 }));
        let oldCount = 0;
        const events = [];
        const inject = () => assert.equal(runtime.call('encos_fake_inject_feedback', [first, 0, 1, 1, [1, 2, 3, 25, 25], [0]]).code, 0);
        runtime.subscribe('motor.onStatus', { handle: motor1 }, () => oldCount++);
        inject();
        runtime.subscribe('motor.onStatus', { handle: motor2 }, event => events.push(event));
        inject();
        await tick(); await tick();
        assert.equal(oldCount, 0);
        assert.equal(events.length, 1);
        inject();
        value(runtime.invoke('motor.dispose', { handle: motor1 }));
        await tick(); await tick();
        assert.equal(events.length, 1);
        assert.notEqual(runtime.invoke('motor.getIndex', { handle: motor2 }).code, 0);
        assert.equal(value(runtime.invoke('bus.selectMotor', { handle: bus2, index: 1 })), null);
    } finally { runtime.dispose(); }
});

test('structured manager creation preserves ownership and deletion releases every local lease', { skip: !addon || !root }, () => {
    const first = create();
    const second = create();
    const options = { type: 'Fake', interfaceName: `structured-manager-${process.pid}` };
    try {
        const adapter = value(first.invoke('manager.createAdapter', options));
        value(first.invoke('runtime.createAdapter', options));
        assert.equal(value(first.invoke('adapter.hasFakeAdapterControl', { handle: adapter })), true);
        assert.notEqual(second.invoke('manager.createAdapter', options).code, 0);
        assert.equal(value(second.invoke('manager.destroyAdapterByInterfaceName', { interfaceName: options.interfaceName })), false);
        assert.equal(value(first.invoke('manager.destroyAdapterByInterfaceName', { interfaceName: options.interfaceName })), true);
        value(second.invoke('runtime.createAdapter', options));
    } finally { first.dispose(); second.dispose(); }
});

test('glove destruction cancels subscriptions on its internal buses before deleting devices', { skip: !addon }, async () => {
    const runtime = create();
    const adapter = scalar(runtime.call('encos_create_fake_adapter', [`structured-glove-${process.pid}`, '', 3]));
    try {
        const glove = value(runtime.invoke('adapter.getGlove', { handle: adapter, slaveIndex: 1 }));
        const bus = value(runtime.invoke('adapter.getBus', { handle: adapter, slaveIndex: 1, busIndex: 0 }));
        const motor = value(await runtime.invokeAsync('bus.getMotor', { handle: bus, index: 100, model: 0 }));
        runtime.subscribe('motor.onStatus', { handle: motor }, () => {});
        value(runtime.invoke('glove.dispose', { handle: glove }));
        assert.notEqual(runtime.invoke('motor.getIndex', { handle: motor }).code, 0);
        assert.notEqual(runtime.invoke('bus.getIndex', { handle: bus }).code, 0);
    } finally { runtime.dispose(); }
});

test('manager frame dispatch preserves explicit payload length independently of storage width', { skip: !addon }, async () => {
    const runtime = create();
    try {
        const adapter = scalar(runtime.call('encos_create_fake_adapter', [`structured-frame-${process.pid}`, '', 3]));
        const bus = value(runtime.invoke('adapter.getBus', { handle: adapter, busIndex: 0 }));
        const battery = value(runtime.invoke('bus.getBattery', { handle: bus, index: 0 }));
        let events = 0;
        runtime.subscribe('battery.onStatus', { handle: battery }, () => events++);
        const args = { handle: adapter, busIndex: 0, message: { id: 0x3f4, frameFlags: 0, data: [1, 80, 0, 0, 0, 0, 0, 0], len: 3 } };
        value(runtime.invoke('manager.dispatchReceive', args));
        await tick(); await tick();
        assert.equal(events, 0);
        args.message.len = 8;
        value(runtime.invoke('manager.dispatchReceive', args));
        await tick(); await tick();
        assert.equal(events, 1);
        args.message.len = 9;
        assert.equal(runtime.invoke('manager.dispatchReceive', args).code, 1);
        args.message.len = -1;
        assert.equal(runtime.invoke('manager.dispatchUnknownReceive', args).code, 1);
    } finally { runtime.dispose(); }
});

test('Worker termination closes live callback channels and releases native ownership', { skip: !addon }, async () => {
    const { Worker } = await import('node:worker_threads');
    const name = `structured-worker-callback-${process.pid}`;
    const worker = new Worker(`
        const { parentPort, workerData } = require('node:worker_threads');
        const r = require(workerData.addonPath).createRuntime(...workerData.paths);
        const h = r.call('encos_create_fake_adapter', [workerData.name, '', 3]).outputs[0][0];
        r.subscribe('fake.decodedCommand', {handle:h}, () => {});
        parentPort.postMessage('ready');
        setInterval(() => {},1000);
    `, { eval: true, workerData: { addonPath, name, paths: root ? [resolve(root, 'runtime/lib/encosPlugins')] : [] } });
    await new Promise((ready, reject) => { worker.once('message', ready); worker.once('error', reject); });
    await worker.terminate();
    const runtime = create();
    try { scalar(runtime.call('encos_create_fake_adapter', [name, '', 3])); }
    finally { runtime.dispose(); }
});

test('invalid subscription handles cannot cancel an existing callback', { skip: !addon }, async () => {
    const runtime = create();
    try {
        const adapter = scalar(runtime.call('encos_create_fake_adapter', [`structured-integer-${process.pid}`, '', 3]));
        const bus = value(runtime.invoke('adapter.getBus', { handle: adapter, busIndex: 0 }));
        const battery = value(runtime.invoke('bus.getBattery', { handle: bus, index: 0 }));
        let count = 0;
        runtime.subscribe('battery.onStatus', { handle: battery }, () => count++);
        assert.throws(() => runtime.subscribe('battery.onStatus', { handle: battery + 0.5 }, null), /integer|range/);
        assert.equal(runtime.invoke('adapter.getBus', { handle: adapter, busIndex: 0.5 }).code, 1);
        assert.equal(runtime.call('encos_fake_inject_raw_message', [adapter, 0, 0x3f4, 0, [1, 80, 0, 0, 0, 0, 0, 0]]).code, 0);
        await tick(); await tick();
        assert.equal(count, 1);
    } finally { runtime.dispose(); }
});

test('resetting motor IDs cleans up subscriptions belonging to retired motors', { skip: !addon }, async () => {
    const runtime = create();
    try {
        const adapter = scalar(runtime.call('encos_create_fake_adapter', [`structured-reset-${process.pid}`, '', 3]));
        const bus = value(runtime.invoke('adapter.getBus', { handle: adapter, busIndex: 0 }));
        for (const index of [1, 2]) {
            assert.equal(runtime.call('encos_fake_seed_motor', [adapter, 0, index, 0]).code, 0);
            const motor = value(await runtime.invokeAsync('bus.getMotor', { handle: bus, index, model: 0 }));
            runtime.subscribe('motor.onStatus', { handle: motor }, () => {});
        }
        value(runtime.invoke('manager.dispatchUnknownReceive', { handle: adapter, busIndex: 0, message: { id: 0x7ff, data: [0x7f, 0x7f], len: 2 } }));
        assert.equal(value(await runtime.invokeAsync('bus.resetMotorsId', { handle: bus, waitForAck: true })), true);
        assert.equal(value(runtime.invoke('bus.getMotors', { handle: bus })).length, 1);
    } finally { runtime.dispose(); }
});

test('logical CAN IDs retain all 32 bits across native adoption and refresh', { skip: !addon }, async () => {
    const runtime = create();
    try {
        const adapter = scalar(runtime.call('encos_create_fake_adapter', [`logical-id-${process.pid}`, '', 3]));
        const bus = value(runtime.invoke('adapter.getBus', { handle: adapter, busIndex: 0 }));
        const handles = [];
        for (const index of [0xe0012345, 0xffffffff]) {
            const handle = value(await runtime.invokeAsync('bus.getMotor', { handle: bus, index, model: 0 }));
            handles.push(handle);
            assert.equal(value(runtime.invoke('bus.selectMotor', { handle: bus, index })), handle);
            assert.equal(value(runtime.invoke('bus.selectMotor', { handle: bus, index: index | 0 })), handle);
        }
        assert.deepEqual(value(runtime.invoke('bus.getMotors', { handle: bus })).map(x => x.handle).sort(), handles.sort());
        value(runtime.invoke('motor.dispose', { handle: handles[0] }));
        assert.equal(value(runtime.invoke('bus.getMotors', { handle: bus })).length, 1);
        assert.equal(value(runtime.invoke('bus.selectMotor', { handle: bus, index: 0xffffffff })), handles[1]);
    } finally { runtime.dispose(); }
});
