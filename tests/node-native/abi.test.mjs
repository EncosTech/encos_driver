// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { resolve } from 'node:path';
import test from 'node:test';

const require = createRequire(import.meta.url);
const addonPath = process.env.ENCOS_NODE_ADDON ?? (process.env.ENCOS_NODE_PACKAGE_ROOT
    ? resolve(process.env.ENCOS_NODE_PACKAGE_ROOT, 'native/encosdriver.node') : undefined);
const addon = addonPath ? require(addonPath) : undefined;
const pluginPath = process.env.ENCOS_NODE_PACKAGE_ROOT
    ? resolve(process.env.ENCOS_NODE_PACKAGE_ROOT, 'runtime/lib/encosPlugins') : undefined;
const createRuntime = () => addon.createRuntime(...(pluginPath ? [pluginPath] : []));

test('native ABI rejects malformed values and unknown operations', { skip: !addon }, () => {
    const runtime = createRuntime();
    try {
        assert.equal(runtime.call('encos_adapter_ok', [-1]).code, 1);
        assert.equal(runtime.call('encos_adapter_ok', [1.5]).code, 1);
        assert.equal(runtime.call('encos_adapter_ok', ['wrong type']).code, 1);
        assert.equal(runtime.call('encos_adapter_ok', []).code, 1);
        assert.equal(runtime.call('not_an_operation', []).code, 7);
        assert.throws(() => runtime.call('encos_adapter_ok', [{}]));
    } finally {
        runtime.dispose();
    }
    assert.equal(runtime.call('encos_adapter_ok', [1]).code, 3);
});

test('native runtimes release exclusive interface leases on adapter disposal', { skip: !addon }, () => {
    const first = createRuntime();
    const second = createRuntime();
    const name = `native-abi-lease-${process.pid}`;
    const create = runtime => runtime.call('encos_create_fake_adapter', [name, '', 3]);
    try {
        const initial = create(first);
        assert.equal(initial.code, 0);
        assert.equal(create(second).code, 5);
        assert.equal(first.call('encos_dispose_adapter', [initial.outputs[0][0]]).code, 0);
        assert.equal(create(second).code, 0);
        assert.equal(first.call('encos_adapter_ok', [initial.outputs[0][0]]).code, 2);
    } finally {
        first.dispose();
        second.dispose();
    }
});

test('native async jobs settle after runtime disposal', { skip: !addon }, async () => {
    const runtime = createRuntime();
    const jobs = Array.from({ length: 16 }, () => runtime.callAsync('encos_adapter_ok', [123]));
    runtime.dispose();
    const results = await Promise.all(jobs);
    assert.ok(results.every(result => result.code === 3));
});

test('Worker termination releases its native interface lease', { skip: !addon }, async () => {
    const { Worker } = await import('node:worker_threads');
    const name = `native-abi-worker-${process.pid}`;
    const worker = new Worker(`
        const { parentPort, workerData } = require('node:worker_threads');
        const runtime = require(workerData.addonPath).createRuntime(...(workerData.pluginPath ? [workerData.pluginPath] : []));
        const created = runtime.call('encos_create_fake_adapter', [workerData.name, '', 3]);
        const adapter = created.outputs[0][0];
        const bus = runtime.call('encos_adapter_get_bus', [adapter, -1, 0]).outputs[0][0];
        runtime.callAsync('encos_bus_get_motor_with_model', [bus, 1, 0]);
        parentPort.postMessage('pending');
        setInterval(() => {}, 1000);
    `, { eval: true, workerData: { addonPath, name, pluginPath } });
    await new Promise((resolveReady, reject) => {
        worker.once('message', resolveReady);
        worker.once('error', reject);
    });
    await worker.terminate();
    const runtime = createRuntime();
    try {
        assert.equal(runtime.call('encos_create_fake_adapter', [name, '', 3]).code, 0);
    } finally {
        runtime.dispose();
    }
});

test('native ABI rejects embedded NUL before converting strings to C pointers', { skip: !addon }, () => {
    const runtime = createRuntime();
    try {
        assert.throws(() => runtime.call('encos_create_fake_adapter', [`native-nul-${process.pid}\0alias`, '', 3]), /NUL/);
        assert.throws(() => runtime.callAsync('encos_create_fake_adapter', [`native-nul-${process.pid}`, 'logger\0alias', 3]), /NUL/);
        assert.throws(() => runtime.call('encos_adapter_ok\0alias', [1]), /NUL/);
        assert.throws(() => addon.createRuntime('plugins\0alias'), /NUL/);
    } finally {
        runtime.dispose();
    }
});

test('asynchronous adapter disposal releases only the final shared interface lease', { skip: !addon }, async () => {
    const first = createRuntime();
    const second = createRuntime();
    const name = `native-abi-async-lease-${process.pid}`;
    const create = runtime => runtime.call('encos_create_fake_adapter', [name, '', 3]);
    try {
        const initial = create(first);
        const shared = create(first);
        assert.equal(initial.code, 0);
        assert.equal(shared.code, 0);
        assert.equal((await first.callAsync('encos_dispose_adapter', [initial.outputs[0][0]])).code, 0);
        assert.equal(create(second).code, 5);
        assert.equal((await first.callAsync('encos_dispose_adapter', [shared.outputs[0][0]])).code, 0);
        assert.equal(create(second).code, 0);
        assert.equal(first.call('encos_adapter_ok', [shared.outputs[0][0]]).code, 2);
    } finally {
        first.dispose();
        second.dispose();
    }
});
