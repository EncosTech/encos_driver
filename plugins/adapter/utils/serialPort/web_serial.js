// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

// Emscripten --pre-js: this bridge belongs to one module instance, not the page globally.
(() => {
    const ports = new Map();
    const opening = new WeakSet();
    const maxQueuedBytes = 65536;
    let nextId = 1;

    function notifyClosed(entry) {
        const receiver = entry.receiver;
        entry.receiver = 0;
        if (receiver) Module['_encos_serial_port_closed'](receiver);
    }

    function closeEntry(entry) {
        if (entry.closing) return entry.closing;
        entry.closed = true;
        notifyClosed(entry);
        entry.queue.length = 0;
        // Start cancellation before waiting for pending read/write promises.
        entry.closing = (async () => {
            const cancellation = [];
            if (entry.reader) cancellation.push(entry.reader.cancel().catch(() => {}));
            if (entry.writer) cancellation.push(entry.writer.abort().catch(() => {}));
            await Promise.all(cancellation);
            await Promise.all([entry.readTask, entry.writeTask]);
            if (entry.reader) entry.reader.releaseLock();
            if (entry.writer) entry.writer.releaseLock();
            try {
                await entry.port.close();
            } finally {
                ports.delete(entry.name);
                opening.delete(entry.port);
            }
        })();
        return entry.closing;
    }

    function fail(entry, error) {
        entry.error = String(error);
        closeEntry(entry).catch(() => {});
    }

    async function readLoop(entry) {
        try {
            while (!entry.closed) {
                const {value, done} = await entry.reader.read();
                if (entry.closed) break;
                if (done) throw new Error('Serial port disconnected');
                if (!value || !value.byteLength) continue;
                for (let offset = 0; offset < value.byteLength && !entry.closed; offset += 4096) {
                    const bytes = value.subarray(offset, offset + 4096);
                    const pointer = Module['_malloc'](bytes.byteLength);
                    if (!pointer) throw new Error('Web Serial receive allocation failed');
                    try {
                        HEAPU8.set(bytes, pointer);
                        if (!Module['_encos_serial_port_receive'](entry.receiver, pointer, bytes.byteLength)) {
                            throw new Error('Serial port receive callback failed');
                        }
                    } finally {
                        Module['_free'](pointer);
                    }
                }
            }
        } catch (error) {
            if (!entry.closed) fail(entry, error);
        }
    }

    async function drainWrites(entry) {
        try {
            while (entry.queue.length && !entry.closed) {
                const bytes = entry.queue.shift();
                // Preserve each caller's write boundary without merging or retrying.
                await entry.writer.write(bytes);
                entry.queuedBytes -= bytes.byteLength;
            }
        } catch (error) {
            if (!entry.closed) fail(entry, error);
        } finally {
            entry.writing = false;
        }
    }

    async function open(port) {
        if (opening.has(port)) throw new Error('Serial port already open in this module');
        opening.add(port);
        try {
            await port.open({baudRate: 115200, dataBits: 8, stopBits: 1, parity: 'none',
                             flowControl: 'none', bufferSize: 4096});
        } catch (error) {
            opening.delete(port);
            throw error;
        }
        const name = `webserial:${nextId++}`;
        ports.set(name, {name, port, receiver: 0, reader: null, writer: null, closed: false,
                        queue: [], queuedBytes: 0, writing: false, error: null});
        return name;
    }

    Module['webSerial'] = {
        // Call directly from a browser click handler so requestPort retains user activation.
        requestPort(options = {}) {
            if (!globalThis.navigator?.serial) {
                return Promise.reject(new Error('Web Serial is unavailable; use a supported secure browser'));
            }
            return navigator.serial.requestPort(options).then(open);
        },
        // For a SerialPort returned by navigator.serial.getPorts() after prior permission.
        open,
        async close(name) {
            const entry = ports.get(name);
            if (entry) await closeEntry(entry);
        },
        status(name) {
            const entry = ports.get(name);
            return entry ? {open: !entry.closed, attached: !!entry.receiver,
                            queuedBytes: entry.queuedBytes, error: entry.error} : {open: false};
        },
        _attach(name, receiver) {
            const entry = ports.get(name);
            if (!entry || entry.closed || entry.receiver || !receiver) return false;
            try {
                entry.reader = entry.port.readable.getReader();
                entry.writer = entry.port.writable.getWriter();
                entry.receiver = receiver;
                entry.readTask = readLoop(entry);
                return true;
            } catch (error) {
                fail(entry, error);
                return false;
            }
        },
        _detach(name, receiver) {
            const entry = ports.get(name);
            if (!entry || entry.receiver !== receiver) return;
            entry.receiver = 0;
            closeEntry(entry).catch(() => {});
        },
        _write(name, bytes) {
            const entry = ports.get(name);
            if (!entry || entry.closed || !entry.receiver) return false;
            if (entry.queuedBytes + bytes.byteLength > maxQueuedBytes) {
                fail(entry, new Error('Web Serial transmit queue overflow'));
                return false;
            }
            entry.queue.push(bytes);
            entry.queuedBytes += bytes.byteLength;
            if (!entry.writing) {
                entry.writing = true;
                entry.writeTask = drainWrites(entry);
            }
            return true;
        }
    };
})();
