#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""将非系统依赖私有化，并检查发布产物的 ELF 兼容基线。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

SYSTEM = {'libc.so.6', 'libm.so.6', 'libdl.so.2', 'libpthread.so.0', 'librt.so.1',
          'libutil.so.1', 'libresolv.so.2', 'libstdc++.so.6', 'libgcc_s.so.1',
          'ld-linux-x86-64.so.2', 'ld-linux-aarch64.so.1'}
SYSTEM_RUNTIME = SYSTEM | {'libsystemd.so.0'}
PRIVILEGED_HELPERS = {'CanFdBrokerExecutable', 'EthercatFdBrokerExecutable', 'ThreadPriorityHelper',
                      'EthernetXdpBrokerExecutable'}
LIMITS = {'GLIBC': (2, 28), 'GLIBCXX': (3, 4, 25), 'CXXABI': (1, 3, 11), 'LIBSYSTEMD': (239,)}


def command(*args, env=None):
    return subprocess.check_output(args, text=True, env=env).strip()


def elf_kind(path):
    with path.open('rb') as stream:
        header = stream.read(20)
    if header[:4] != b'\x7fELF':
        return None
    if len(header) != 20 or header[5] not in (1, 2):
        raise RuntimeError(f'{path}: invalid ELF header')
    order = 'little' if header[5] == 1 else 'big'
    kind = int.from_bytes(header[16:18], order)
    machine = int.from_bytes(header[18:20], order)
    if kind == 1 and machine == 247:
        return 'bpf'
    if kind not in (2, 3):
        raise RuntimeError(f'{path}: unexpected ELF type {kind}')
    return 'native'


def elf_files(root, kind='native'):
    return sorted({path.resolve() for path in root.rglob('*')
                   if path.is_file() and not any(path.relative_to(root).parts[i:i + 2] == ('share', 'doc')
                               for i in range(len(path.relative_to(root).parts) - 1))
                   and elf_kind(path) == kind})


def search_directories(root, private_directory, core_directory='lib'):
    return list(dict.fromkeys([root / private_directory, root / core_directory]))


def set_rpath(path, directories):
    origins = []
    for directory in directories:
        relative = os.path.relpath(directory, path.parent)
        origins.append('$ORIGIN' if relative == '.' else '$ORIGIN/' + relative)
    command('patchelf', '--set-rpath', ':'.join(origins), str(path))


def bundle(root, private_directory='lib', library_directories=(), core_directory='lib'):
    lib = root / private_directory
    lib.mkdir(parents=True, exist_ok=True)
    directories = search_directories(root, private_directory, core_directory)
    mapping = {}
    queue = elf_files(root)
    lookup = directories + list(library_directories)
    env = {**os.environ, 'LD_LIBRARY_PATH': ':'.join(map(str, lookup))}
    for path in queue:
        set_rpath(path, directories)
    index = 0
    while index < len(queue):
        path = queue[index]
        index += 1
        needed = command('patchelf', '--print-needed', str(path)).splitlines()
        resolved = {}
        for line in command('ldd', str(path), env=env).splitlines():
            match = re.match(r'\s*(\S+) => (\S+)', line)
            if match and match[2].startswith('/'):
                resolved[match[1]] = Path(match[2]).resolve()
        for name in needed:
            if name in SYSTEM_RUNTIME or any((d / name).exists() for d in directories):
                continue
            source = resolved.get(name)
            if source is None:
                raise RuntimeError(f'{path}: dependency not found: {name}')
            if name not in mapping:
                digest = hashlib.sha256(source.read_bytes()).hexdigest()[:12]
                private_name = f'libencos-private-{digest}-{name}'
                target = lib / private_name
                shutil.copy2(source, target)
                command('patchelf', '--set-soname', private_name, str(target))
                set_rpath(target, directories)
                mapping[name] = private_name
                queue.append(target)
            command('patchelf', '--replace-needed', name, mapping[name], str(path))
    return mapping


def audit(root, private_directory='lib', core_directory='lib'):
    directories = search_directories(root, private_directory, core_directory)
    maxima = {key: (0,) for key in LIMITS}
    files = elf_files(root)
    if not files:
        raise RuntimeError('No ELF files found')
    for path in files:
        symbols = command('readelf', '--dyn-syms', '--wide', str(path))
        for line in symbols.splitlines():
            fields = line.split()
            if (len(fields) >= 8 and fields[6] != 'UND'
                    and re.match(r'^_Z(?:NK?St10filesystem|T[ISV]NSt10filesystem)', fields[7])):
                raise RuntimeError(f'{path}: exported private filesystem implementation {fields[7]}')
        for name in command('patchelf', '--print-needed', str(path)).splitlines():
            if path.name in PRIVILEGED_HELPERS and name not in SYSTEM:
                raise RuntimeError(f'{path}: privileged helper must not depend on {name}')
            if name not in SYSTEM_RUNTIME and not any((d / name).exists() for d in directories):
                raise RuntimeError(f'{path}: unbundled dependency {name}')
        versions = command('readelf', '--version-info', str(path))
        for family, version in re.findall(r'\b(GLIBCXX|GLIBC|CXXABI|LIBSYSTEMD)_([0-9]+(?:\.[0-9]+)*)', versions):
            parsed = tuple(map(int, version.split('.')))
            maxima[family] = max(maxima[family], parsed)
            if parsed > LIMITS[family]:
                raise RuntimeError(f'{path}: {family}_{version} exceeds {LIMITS[family]}')
        rpath = command('patchelf', '--print-rpath', str(path))
        if any(not item.startswith('$ORIGIN') for item in rpath.split(':')):
            raise RuntimeError(f'{path}: non-relocatable RPATH {rpath}')
    return {'elf_count': len(files), 'bpf_count': len(elf_files(root, 'bpf')),
            'bundled_libraries': sorted(p.name for p in (root / private_directory).glob('libencos-private-*')),
            'required_versions': {
        key: '.'.join(map(str, value)) for key, value in maxima.items()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--audit-only', action='store_true')
    parser.add_argument('--private-directory', default='lib')
    parser.add_argument('--core-directory', default='lib')
    parser.add_argument('--library-directory', type=Path, action='append', default=[])
    args = parser.parse_args()
    private = Path(args.private_directory)
    if private.is_absolute() or '..' in private.parts:
        parser.error('Private directory must stay inside the package prefix')
    core = Path(args.core_directory)
    if core.is_absolute() or '..' in core.parts:
        parser.error('Core directory must stay inside the package prefix')
    root = args.prefix.resolve()
    dependencies = {} if args.audit_only else bundle(root, private, args.library_directory, core)
    result = audit(root, private, core)
    result['private_dependencies'] = dependencies
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
