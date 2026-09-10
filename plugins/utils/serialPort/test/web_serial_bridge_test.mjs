import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';
import test from 'node:test';

const source = readFileSync(new URL('../web_serial.js', import.meta.url), 'utf8');
const tick = () => new Promise(resolve => setImmediate(resolve));

function bridge(navigator) {
    const heap = new Uint8Array(8192);
    const received = [], closed = [];
    const module = {
        _malloc: () => 8,
        _free: () => {},
        _encos_serial_port_receive: (receiver, ptr, size) => {
            received.push({receiver, bytes: [...heap.slice(ptr, ptr + size)]});
            return 1;
        },
        _encos_serial_port_closed: receiver => closed.push(receiver),
    };
    runInNewContext(source, {Module: module, HEAPU8: heap, navigator});
    return {api: module.webSerial, received, closed, module};
}

function port(write = async () => {}) {
    const state = {writes: [], opened: 0, closed: 0, cancelled: 0, controller: null};
    const device = {
        readable: new ReadableStream({
            start(controller) { state.controller = controller; },
            cancel() { ++state.cancelled; },
        }),
        writable: new WritableStream({
            async write(bytes) { state.writes.push([...bytes]); await write(bytes); },
        }),
        async open(options) {
            assert.equal(options.baudRate, 115200);
            ++state.opened;
        },
        async close() {
            assert.equal(this.readable.locked, false);
            assert.equal(this.writable.locked, false);
            ++state.closed;
        },
    };
    return {device, state};
}

test('missing Web Serial and permission rejection are explicit', async () => {
    await assert.rejects(bridge(undefined).api.requestPort(), /unavailable/);
    const error = new Error('permission denied');
    const denied = bridge({serial: {requestPort: () => Promise.reject(error)}});
    await assert.rejects(denied.api.requestPort(), /permission denied/);
});

test('request happens synchronously and open failures release the ownership reservation', async () => {
    let requested = false;
    const {device, state} = port();
    const {api} = bridge({serial: {requestPort: () => { requested = true; return Promise.resolve(device); }}});
    const opening = api.requestPort();
    assert.equal(requested, true);
    const name = await opening;
    assert.equal(state.opened, 1);
    await assert.rejects(api.open(device), /already open/);
    await api.close(name);
    const broken = {open: async () => { throw new Error('open failed'); }};
    await assert.rejects(api.open(broken), /open failed/);
    await assert.rejects(api.open(broken), /open failed/);
});

test('independent FIFO writes and chunk delivery preserve bytes; close cancels pending read', async () => {
    const {api, received, closed} = bridge();
    const {device, state} = port();
    const name = await api.open(device);
    assert.equal(api._attach(name, 21), true);
    assert.equal(api._attach(name, 22), false);
    assert.equal(api._write(name, new Uint8Array([1, 2])), true);
    assert.equal(api._write(name, new Uint8Array([3])), true);
    state.controller.enqueue(new Uint8Array([0xaa, 0x00]));
    state.controller.enqueue(new Uint8Array([1, 2, 3]));
    await tick();
    assert.deepEqual(state.writes, [[1, 2], [3]]);
    assert.deepEqual(received.map(item => item.bytes), [[0xaa, 0x00], [1, 2, 3]]);
    assert.equal(api.status(name).queuedBytes, 0);
    await api.close(name);
    assert.deepEqual(closed, [21]);
    assert.equal(state.closed, 1);
    assert.equal(state.cancelled, 1);
    assert.equal(api._write(name, new Uint8Array([4])), false);
    await api.close(name);
    assert.equal(state.closed, 1);
});

test('reader disconnect and writer failure report closure once without retries', async () => {
    for (const readFailure of [true, false]) {
        const {api, closed} = bridge();
        const {device, state} = port(async () => { if (!readFailure) throw new Error('write failed'); });
        const name = await api.open(device);
        api._attach(name, 9);
        if (readFailure) state.controller.error(new Error('unplugged'));
        else api._write(name, new Uint8Array([1]));
        await tick();
        await api.close(name);
        assert.deepEqual(closed, [9]);
        assert.equal(state.closed, 1);
        assert.equal(state.writes.length, readFailure ? 0 : 1);
    }
});

test('bounded transmit queue fails closed under backpressure and waits for cancellation', async () => {
    let release;
    const gate = new Promise(resolve => { release = resolve; });
    const {api, closed} = bridge();
    const {device, state} = port(() => gate);
    const name = await api.open(device);
    api._attach(name, 13);
    assert.equal(api._write(name, new Uint8Array(65536)), true);
    await tick();
    assert.equal(api._write(name, new Uint8Array([1])), false);
    assert.deepEqual(closed, [13]);
    release();
    await api.close(name);
    assert.equal(state.writes.length, 1);
    assert.equal(state.closed, 1);
});

test('separate module instances do not share port ownership or callbacks', async () => {
    const first = bridge(), second = bridge();
    const a = port(), b = port();
    const nameA = await first.api.open(a.device);
    const nameB = await second.api.open(b.device);
    first.api._attach(nameA, 1);
    second.api._attach(nameB, 2);
    a.state.controller.enqueue(new Uint8Array([7]));
    await tick();
    assert.equal(first.received.length, 1);
    assert.equal(second.received.length, 0);
    await Promise.all([first.api.close(nameA), second.api.close(nameB)]);
});

test('detach clears callbacks synchronously and only the attached owner can close', async () => {
    const {api, received, closed} = bridge();
    const {device, state} = port();
    const name = await api.open(device);
    assert.equal(api._attach(name, 31), true);
    api._detach(name, 32);
    assert.equal(api.status(name).attached, true);
    state.controller.enqueue(new Uint8Array([1, 2, 3]));
    api._detach(name, 31);
    assert.equal(api.status(name).attached, false);
    assert.equal(api.status(name).open, false);
    assert.equal(api._attach(name, 33), false);
    await api.close(name);
    assert.deepEqual(received, []);
    assert.deepEqual(closed, []);
    assert.equal(state.closed, 1);
});

test('large receives preserve bytes across bounded callback chunks', async () => {
    const {api, received} = bridge();
    const {device, state} = port();
    const name = await api.open(device);
    api._attach(name, 41);
    const bytes = Uint8Array.from({length: 9000}, (_, index) => index % 256);
    state.controller.enqueue(bytes);
    await tick();
    assert.deepEqual(received.map(item => item.bytes.length), [4096, 4096, 808]);
    assert.deepEqual(received.flatMap(item => item.bytes), [...bytes]);
    await api.close(name);
});

test('receive callback failure closes once and prevents subsequent delivery', async () => {
    const {api, module, closed} = bridge();
    const {device, state} = port();
    const name = await api.open(device);
    let calls = 0;
    module._encos_serial_port_receive = () => { ++calls; return 0; };
    api._attach(name, 51);
    state.controller.enqueue(new Uint8Array(5000));
    await tick();
    await api.close(name);
    assert.equal(calls, 1);
    assert.deepEqual(closed, [51]);
    assert.equal(state.closed, 1);
});
