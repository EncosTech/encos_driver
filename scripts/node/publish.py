#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""先验证全部平台与集合包，依次发布平台包、集合包的原始归档；默认只校验。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

PLATFORMS = {'linux-x64-gnu': ('linux', 'x64'), 'linux-arm64-gnu': ('linux', 'arm64'),
             'win32-x64': ('win32', 'x64')}
REGISTRY = 'https://git.in.encos.cn/api/packages/packages/npm/'


def validate_entries(source, package, entry, types, required=()):
    if (package.get('type'), package.get('main'), package.get('types')) != ('module', entry, types):
        raise ValueError('Archive entry metadata mismatch')
    if package.get('exports') != {'.': {'types': types, 'import': entry}}:
        raise ValueError('Archive exports metadata mismatch')
    for path in (entry[2:], types[2:], *required):
        name = 'package/' + path
        try:
            member = source.getmember(name)
        except KeyError as error:
            raise ValueError(f'Missing package entry: {name}') from error
        if not member.isfile():
            raise ValueError(f'Invalid package entry: {name}')


def selector_candidate(root, version):
    directory = root / 'selector'
    manifest = json.loads((directory / 'selector-manifest.json').read_text())
    name = '@encos/encos-driver-native'
    dependencies = {'@encos/encos-driver-native-' + suffix: version for suffix in PLATFORMS}
    if manifest['package'] != name or manifest['version'] != version:
        raise ValueError('Selector identity or version mismatch')
    if manifest.get('optionalDependencies') != dependencies:
        raise ValueError('Selector dependencies mismatch')
    filename = f'encos-encos-driver-native-{version}.tgz'
    if manifest['filename'] != filename or '/' in filename or '\\' in filename:
        raise ValueError('Invalid selector filename')
    archive = directory / filename
    if archive.is_symlink() or hashlib.sha256(archive.read_bytes()).hexdigest() != manifest['sha256']:
        raise ValueError('Checksum mismatch: selector')
    with tarfile.open(archive) as source:
        package = json.load(source.extractfile('package/package.json'))
        if (package['name'], package['version']) != (name, version):
            raise ValueError('Archive metadata mismatch: selector')
        if package.get('optionalDependencies') != dependencies or package.get('dependencies'):
            raise ValueError('Selector archive dependencies mismatch')
        if any(key in package.get('scripts', {}) for key in ('preinstall', 'install', 'postinstall')):
            raise ValueError('Selector must not have install scripts')
        if any(key in package for key in ('os', 'cpu', 'libc')):
            raise ValueError('Selector must not restrict platform metadata')
        validate_entries(source, package, './dist/index.js', './types/index-native.d.ts', ('dist/loader.js',))
    return archive


def candidates(root):
    result = []
    versions = set()
    for suffix, (system, arch) in PLATFORMS.items():
        directory = root / suffix
        manifest = json.loads((directory / 'node-manifest.json').read_text())
        name = '@encos/encos-driver-native-' + suffix
        if (manifest['package'], manifest['platform'], manifest['arch'], manifest['napi']) != (name, system, arch, 8):
            raise ValueError(f'Invalid platform identity: {suffix}')
        version = manifest['version']
        filename = f'encos-encos-driver-native-{suffix}-{version}.tgz'
        if manifest['filename'] != filename or '/' in filename or '\\' in filename:
            raise ValueError('Invalid package filename')
        archive = directory / filename
        if archive.is_symlink() or hashlib.sha256(archive.read_bytes()).hexdigest() != manifest['sha256']:
            raise ValueError(f'Checksum mismatch: {suffix}')
        with tarfile.open(archive) as source:
            package = json.load(source.extractfile('package/package.json'))
            if (package['name'], package['version'], package['os'], package['cpu']) != (name, version, [system], [arch]):
                raise ValueError(f'Archive metadata mismatch: {suffix}')
            if system == 'linux' and package.get('libc') != ['glibc']:
                raise ValueError('Linux package must declare glibc')
            validate_entries(source, package, './dist/index-native.js', './dist/index-native.d.ts',
                             ('native/encosdriver.node',))
            names = set(source.getnames())
            if any('third-party-sources/' in item or item.endswith('.wasm') for item in names):
                raise ValueError('Unexpected source or WASM artifact')
        versions.add(version)
        result.append(archive)
    if len(versions) != 1:
        raise ValueError('Platform versions differ')
    result.append(selector_candidate(root, next(iter(versions))))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--publish', action='store_true')
    args = parser.parse_args()
    archives = candidates(args.directory)
    if not args.publish:
        for archive in archives:
            print(f'Verified: {archive.name}')
        return
    token = os.environ['USER_PASSWORD']
    with tempfile.TemporaryDirectory(prefix='encos-node-publish-') as work:
        config = Path(work) / '.npmrc'
        config.write_text(f'@encos:registry={REGISTRY}\n//git.in.encos.cn/api/packages/packages/npm/:_authToken={token}\n')
        config.chmod(0o600)
        env = {**os.environ, 'NPM_CONFIG_USERCONFIG': str(config)}
        for archive in archives:
            subprocess.run(['npm', 'publish', str(archive.resolve()), '--ignore-scripts', '--registry', REGISTRY],
                           cwd=work, env=env, check=True)


if __name__ == '__main__':
    main()
