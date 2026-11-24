// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createPermissionApi } from '../../npm/dist/permissions-native.js';

function fixture(options = {}) {
  let authorized = false;
  const calls = [];
  const api = createPermissionApi({
    platform: options.platform ?? 'linux',
    packageRoot: '/linked package',
    realpath: async path => {
      if (options.missing && path.includes('Executable')) throw new Error('ENOENT');
      return path.replace('/linked package', '/pnpm store/package');
    },
    execute: async (file, args) => {
      calls.push([file, args]);
      if (file === 'pkexec') {
        if (options.error) throw Object.assign(new Error('failed'), {code: options.error});
        authorized = !options.ineffective;
        return {stdout: ''};
      }
      if (options.getcapError) throw Object.assign(new Error('missing'), {code: 'ENOENT'});
      if (options.capabilities) return {stdout: `${args[0]} ${options.capabilities}\n`};
      return {stdout: authorized ? `${args[0]} cap_net_raw,cap_net_admin,cap_bpf,cap_sys_nice=ep\n` : ''};
    },
  });
  return { api, calls };
}

test('Windows never checks files or invokes authorization', async () => {
  const {api, calls} = fixture({platform: 'win32'});
  assert.deepEqual(await api.requestPermissions(), {required: false, authorized: true, missing: []});
  assert.equal(calls.length, 0);
});
test('Linux checks every helper and rechecks actual capabilities after authorization', async () => {
  const {api, calls} = fixture();
  const before = await api.checkPermissions();
  assert.equal(before.missing.length, 4);
  assert.deepEqual(before.missing[2].capabilities, ['cap_net_raw','cap_net_admin','cap_bpf']);
  assert.equal((await api.requestPermissions()).authorized, true);
  assert.deepEqual(calls.filter(([file]) => file === 'pkexec'), [['pkexec', ['/bin/bash', '/pnpm store/package/scripts/authorize.sh']]]);
  const count = calls.length;
  await api.checkPermissions();
  assert.equal(calls.length, count + 4);
});
test('missing helpers fail closed', async () => {
  const {api} = fixture({missing: true});
  assert.equal((await api.checkPermissions()).missing.length, 4);
});
for (const [error, message] of [['ENOENT', /not installed/], [126, /cancelled or denied/], [1, /command failed/]]) {
  test(`authorization failure ${error} is actionable`, async () => {
    await assert.rejects(fixture({error}).api.requestPermissions(), message);
  });
}
test('successful command without capabilities is rejected', async () => {
  await assert.rejects(fixture({ineffective: true}).api.requestPermissions(), /still missing/);
});
test('missing capability inspection tool is actionable', async () => {
  await assert.rejects(fixture({getcapError: true}).api.checkPermissions(), /getcap/);
});

test('permitted-only capabilities do not authorize helper execution', async () => {
  const {api} = fixture({capabilities: 'cap_net_raw,cap_net_admin,cap_bpf,cap_sys_nice=p'});
  assert.equal((await api.checkPermissions()).missing.length, 4);
});
test('reports only missing capabilities for partially authorized helpers', async () => {
  const {api} = fixture({capabilities: 'cap_net_raw,cap_net_admin=ep'});
  const status = await api.checkPermissions();
  assert.equal(status.missing.length, 2);
  assert.deepEqual(status.missing[0].capabilities, ['cap_bpf']);
  assert.deepEqual(status.missing[1].capabilities, ['cap_sys_nice']);
});

test('bundled authorization script resolves spaces and invokes all fixed installed scripts', {skip: process.platform !== 'linux'}, async () => {
  const {mkdtemp, mkdir, copyFile, writeFile, readFile, rm} = await import('node:fs/promises');
  const {tmpdir} = await import('node:os');
  const {join} = await import('node:path');
  const {execFileSync} = await import('node:child_process');
  const root = await mkdtemp(join(tmpdir(), 'encos permissions '));
  try {
    const scripts = join(root, 'scripts');
    const helpers = join(root, 'runtime/lib/encosPlugins');
    await mkdir(scripts, {recursive: true});
    await mkdir(helpers, {recursive: true});
    const authorize = join(scripts, 'authorize.sh');
    await copyFile(new URL('../../scripts/node/authorize.sh', import.meta.url), authorize);
    const names = ['set_socketcan_capabilities.sh', 'set_ethercat_capabilities.sh', 'set_ethernet_capabilities.sh', 'set_thread_priority_capability.sh'];
    for (const name of names) {
      await writeFile(join(helpers, name), '#!/bin/bash\n# SPDX-FileCopyrightText: 2026 Encos\n# SPDX-License-Identifier: MIT\nprintf "%s\\n" "$0" >> "$1/invoked"\n');
    }
    execFileSync('/bin/bash', [authorize]);
    assert.deepEqual((await readFile(join(root, 'runtime/invoked'), 'utf8')).trim().split('\n'), names.map(name => join(helpers, name)));
    assert.throws(() => execFileSync('/bin/bash', [authorize, '/arbitrary/path'], {stdio: 'pipe'}));
  } finally {
    await rm(root, {recursive: true, force: true});
  }
});
