#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""原生 npm 候选发布校验，测试中不访问网络或执行上传。"""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import subprocess

spec = importlib.util.spec_from_file_location('node_publish', Path(__file__).resolve().parents[1] / 'node/publish.py')
publish = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publish)


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for suffix in publish.PLATFORMS:
            self.candidate(suffix)
        self.selector()

    def candidate(self, suffix, version='1.2.3', package_arch=None):
        system, arch = publish.PLATFORMS[suffix]
        directory = self.root / suffix
        directory.mkdir(exist_ok=True)
        name = '@encos/encos-driver-native-' + suffix
        filename = f'encos-encos-driver-native-{suffix}-{version}.tgz'
        package = {'name': name, 'version': version, 'os': [system], 'cpu': [package_arch or arch]}
        package.update({'type': 'module', 'main': './dist/index-native.js',
                        'types': './dist/index-native.d.ts',
                        'exports': {'.': {'types': './dist/index-native.d.ts', 'import': './dist/index-native.js'}}})
        if system == 'linux':
            package['libc'] = ['glibc']
        with tarfile.open(directory / filename, 'w:gz') as archive:
            for path, content in {
                'package/package.json': json.dumps(package).encode(),
                'package/native/encosdriver.node': b'fixture',
                'package/dist/index-native.js': b'fixture',
                'package/dist/index-native.d.ts': b'fixture',
            }.items():
                member = tarfile.TarInfo(path)
                member.size = len(content)
                archive.addfile(member, io.BytesIO(content))
        (directory / 'node-manifest.json').write_text(json.dumps({
            'package': name, 'version': version, 'platform': system, 'arch': arch, 'napi': 8,
            'filename': filename, 'sha256': hashlib.sha256((directory / filename).read_bytes()).hexdigest(),
        }))

    def selector(self, version='1.2.3', dependencies=None, package_changes=None, missing_entry=None):
        directory = self.root / 'selector'
        directory.mkdir(exist_ok=True)
        name = '@encos/encos-driver-native'
        filename = f'encos-encos-driver-native-{version}.tgz'
        dependencies = dependencies if dependencies is not None else {
            '@encos/encos-driver-native-' + suffix: version for suffix in publish.PLATFORMS}
        package = {'name': name, 'version': version, 'type': 'module',
                   'main': './dist/index.js', 'types': './types/index-native.d.ts',
                   'exports': {'.': {'types': './types/index-native.d.ts', 'import': './dist/index.js'}},
                   'optionalDependencies': dependencies}
        package.update(package_changes or {})
        with tarfile.open(directory / filename, 'w:gz') as archive:
            for path, content in {
                'package/package.json': json.dumps(package).encode(),
                'package/dist/index.js': b'fixture',
                'package/dist/loader.js': b'fixture',
                'package/types/index-native.d.ts': b'fixture',
            }.items():
                if path == missing_entry:
                    continue
                member = tarfile.TarInfo(path)
                member.size = len(content)
                archive.addfile(member, io.BytesIO(content))
        (directory / 'selector-manifest.json').write_text(json.dumps({
            'package': name, 'version': version, 'filename': filename,
            'sha256': hashlib.sha256((directory / filename).read_bytes()).hexdigest(),
            'optionalDependencies': dependencies,
        }))

    def test_complete_candidates(self):
        archives = publish.candidates(self.root)
        self.assertEqual(len(archives), 4)
        self.assertEqual(archives[-1].parent.name, 'selector')

    def test_missing_selector(self):
        (self.root / 'selector/selector-manifest.json').unlink()
        with self.assertRaises(FileNotFoundError):
            publish.candidates(self.root)

    def test_tampered_selector(self):
        next((self.root / 'selector').glob('*.tgz')).write_bytes(b'bad')
        with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
            publish.candidates(self.root)

    def test_selector_version_mismatch(self):
        self.selector(version='1.2.4')
        with self.assertRaisesRegex(ValueError, 'version'):
            publish.candidates(self.root)

    def test_selector_dependencies_must_match_all_platforms_exactly(self):
        valid = {'@encos/encos-driver-native-' + suffix: '1.2.3' for suffix in publish.PLATFORMS}
        for dependencies in ({}, {**valid, next(iter(valid)): '^1.2.3'},
                             {**valid, '@encos/unexpected': '1.2.3'}):
            with self.subTest(dependencies=dependencies):
                self.selector(dependencies=dependencies)
                with self.assertRaisesRegex(ValueError, 'dependencies'):
                    publish.candidates(self.root)

    def test_selector_archive_dependencies_match_manifest(self):
        self.selector(package_changes={'optionalDependencies': {}})
        with self.assertRaisesRegex(ValueError, 'dependencies'):
            publish.candidates(self.root)

    def test_selector_entry_metadata_and_files(self):
        for changes in ({'main': './wrong.js'}, {'types': './wrong.d.ts'},
                        {'type': 'commonjs'}, {'exports': {}}, {'dependencies': {'other': '1'}},
                        {'scripts': {'postinstall': 'node install.js'}}, {'os': ['linux']},
                        {'version': '1.2.4'}, {'name': '@encos/wrong'}):
            with self.subTest(changes=changes):
                self.selector(package_changes=changes)
                with self.assertRaises(ValueError):
                    publish.candidates(self.root)
        self.selector(missing_entry='package/types/index-native.d.ts')
        with self.assertRaisesRegex(ValueError, 'Missing package entry'):
            publish.candidates(self.root)

    def test_invalid_selector_manifest(self):
        path = self.root / 'selector/selector-manifest.json'
        original = json.loads(path.read_text())
        for changes in ({'filename': '../outside.tgz'}, {'package': '@encos/wrong'}):
            with self.subTest(changes=changes):
                path.write_text(json.dumps({**original, **changes}))
                with self.assertRaises(ValueError):
                    publish.candidates(self.root)

    def test_default_dry_run_never_publishes(self):
        with patch('sys.argv', ['publish.py', str(self.root)]), patch.object(publish.subprocess, 'run') as run:
            publish.main()
        run.assert_not_called()

    def test_publication_order(self):
        with patch('sys.argv', ['publish.py', str(self.root), '--publish']), \
                patch.dict(publish.os.environ, {'USER_PASSWORD': 'fixture'}), \
                patch.object(publish.subprocess, 'run') as run:
            publish.main()
        self.assertEqual([Path(call.args[0][2]).parent.name for call in run.call_args_list],
                         [*publish.PLATFORMS, 'selector'])

    def test_invalid_selector_prevents_any_publication(self):
        self.selector(dependencies={})
        with patch('sys.argv', ['publish.py', str(self.root), '--publish']), \
                patch.object(publish.subprocess, 'run') as run:
            with self.assertRaises(ValueError):
                publish.main()
        run.assert_not_called()

    def test_platform_failure_prevents_selector_publication(self):
        with patch('sys.argv', ['publish.py', str(self.root), '--publish']), \
                patch.dict(publish.os.environ, {'USER_PASSWORD': 'fixture'}), \
                patch.object(publish.subprocess, 'run', side_effect=[None, subprocess.CalledProcessError(1, 'npm')]) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                publish.main()
        self.assertEqual([Path(call.args[0][2]).parent.name for call in run.call_args_list],
                         list(publish.PLATFORMS)[:2])

    def test_version_mismatch(self):
        self.candidate('win32-x64', '1.2.4')
        with self.assertRaisesRegex(ValueError, 'versions differ'):
            publish.candidates(self.root)

    def test_tampered_archive(self):
        next((self.root / 'linux-x64-gnu').glob('*.tgz')).write_bytes(b'bad')
        with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
            publish.candidates(self.root)

    def test_wrong_architecture_inside_package(self):
        self.candidate('linux-arm64-gnu', package_arch='x64')
        with self.assertRaisesRegex(ValueError, 'Archive metadata mismatch'):
            publish.candidates(self.root)

    def test_missing_platform(self):
        (self.root / 'win32-x64/node-manifest.json').unlink()
        with self.assertRaises(FileNotFoundError):
            publish.candidates(self.root)


if __name__ == '__main__':
    unittest.main()
