#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""统一 Linux 软件包安装树与元数据约束的离线回归测试。"""
import importlib.util
import io
import tarfile
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'package_config', Path(__file__).resolve().parents[1] / 'portable/package-config.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)

stage_spec = importlib.util.spec_from_file_location(
    'stage_plugins', Path(__file__).resolve().parents[1] / 'portable/stage-plugins.py')
stage = importlib.util.module_from_spec(stage_spec)
stage_spec.loader.exec_module(stage)


class LicenseExtractionTests(unittest.TestCase):
    def test_extracts_license_without_extracting_archive_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'sources.tar'
            with tarfile.open(archive, 'w') as output:
                member = tarfile.TarInfo('xdp-tools/LICENSES/GPL-2.0')
                member.size = len(b'GPL fixture')
                output.addfile(member, io.BytesIO(b'GPL fixture'))
                unwanted = tarfile.TarInfo('../unwanted')
                unwanted.size = 1
                output.addfile(unwanted, io.BytesIO(b'x'))
            destination = root / 'license.txt'
            stage.copy_xdp_bpf_license(archive, destination)
            self.assertEqual(destination.read_bytes(), b'GPL fixture')
            self.assertEqual({path.name for path in root.iterdir()}, {'sources.tar', 'license.txt'})

    def test_symlink_license_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'sources.tar'
            with tarfile.open(archive, 'w') as output:
                member = tarfile.TarInfo('xdp-tools/LICENSES/GPL-2.0')
                member.type = tarfile.SYMTYPE
                member.linkname = '/etc/passwd'
                output.addfile(member)
            with self.assertRaisesRegex(RuntimeError, 'regular file'):
                stage.copy_xdp_bpf_license(archive, root / 'license.txt')


class PackageLayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.prefix = self.root / 'stage/usr'
        for plugin in package.PLUGINS:
            self.add(f'lib/encosPlugins/lib{plugin}Plugin.so')
        for helper in (*package.HELPERS, 'EthernetXdpBrokerExecutable', 'ethernet_xdp.bpf.o'):
            self.add(f'lib/encosPlugins/{helper}')
        for path in package.BASE_FILES:
            self.add(path)
        for directory in ('relink/ethernet',):
            self.add(f'share/doc/libencosdriver/{directory}/fixture')

    def add(self, path):
        target = self.prefix / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(b'fixture')
        return target

    def test_all_adapters_and_sdk_accepted(self):
        package.validate(self.prefix)

    def test_each_adapter_is_required(self):
        for plugin in package.PLUGINS:
            with self.subTest(plugin=plugin):
                path = f'lib/encosPlugins/lib{plugin}Plugin.so'
                (self.prefix / path).unlink()
                with self.assertRaisesRegex(ValueError, 'Unexpected plugins'):
                    package.validate(self.prefix)
                self.add(path)

    def test_dangling_plugin_is_rejected(self):
        plugin = self.prefix / 'lib/encosPlugins/libEthercatIGHPlugin.so'
        plugin.unlink()
        plugin.symlink_to('missing.so')
        with self.assertRaisesRegex(ValueError, 'Unexpected plugins'):
            package.validate(self.prefix)

    def test_each_helper_and_bpf_is_required(self):
        for helper in (*package.HELPERS, 'EthernetXdpBrokerExecutable', 'ethernet_xdp.bpf.o'):
            with self.subTest(helper=helper):
                path = f'lib/encosPlugins/{helper}'
                (self.prefix / path).unlink()
                with self.assertRaisesRegex(ValueError, 'Missing runtime helper'):
                    package.validate(self.prefix)
                self.add(path)

    def test_unexpected_plugin_is_rejected(self):
        self.add('lib/encosPlugins/libUnknownPlugin.so')
        with self.assertRaisesRegex(ValueError, 'Unexpected plugins'):
            package.validate(self.prefix)

    def test_core_sdk_files_required(self):
        for path in package.BASE_FILES:
            with self.subTest(path=path):
                (self.prefix / path).unlink()
                with self.assertRaisesRegex(ValueError, 'Missing SDK'):
                    package.validate(self.prefix)
                self.add(path)

    def test_split_documentation_is_rejected(self):
        self.add('share/doc/libencosdriver-plugin-igh/copyright')
        with self.assertRaisesRegex(ValueError, 'split package documentation'):
            package.validate(self.prefix)

    def test_missing_relink_material_rejected(self):
        for directory in ('relink/ethernet',):
            with self.subTest(directory=directory):
                path = f'share/doc/libencosdriver/{directory}/fixture'
                (self.prefix / path).unlink()
                with self.assertRaisesRegex(ValueError, 'Missing relink'):
                    package.validate(self.prefix)
                self.add(path)

    def test_source_archives_are_rejected(self):
        self.add('share/doc/libencosdriver/third-party-sources/igh/ethercat.tar')
        with self.assertRaisesRegex(ValueError, 'Source archives must not'):
            package.validate(self.prefix)

    def test_unified_package_metadata_and_hooks(self):
        repo = Path(__file__).resolve().parents[2]
        package.generate(repo, self.root)
        config = (self.root / 'PortableCPackConfig.cmake').read_text()
        expected = {
            'CPACK_PACKAGE_NAME': 'libencosdriver',
            'CPACK_DEBIAN_PACKAGE_DEPENDS': 'libc6 (>= 2.28), libstdc++6 (>= 8), libgcc-s1 | libgcc1, libsystemd0, polkitd',
            'CPACK_DEBIAN_PACKAGE_RECOMMENDS': 'libcap2-bin',
            'CPACK_DEBIAN_PACKAGE_SUGGESTS': 'pkexec, iproute2',
            'CPACK_RPM_PACKAGE_REQUIRES': 'glibc >= 2.28, libstdc++ >= 8, libgcc, systemd-libs, polkit',
            'CPACK_RPM_PACKAGE_RECOMMENDS': 'libcap',
            'CPACK_RPM_PACKAGE_SUGGESTS': 'iproute',
            'CPACK_RPM_PACKAGE_LICENSE': package.LICENSE,
        }
        for key, value in expected.items():
            self.assertIn(f'set({key} "{value}")', config)
        self.assertNotIn('plugin-igh', config)
        self.assertNotIn('plugin-ethernet', config)
        hooks = (self.root / 'control/postinst').read_text()
        for helper in package.HELPERS:
            self.assertIn(f'bash /usr/lib/encosPlugins/{helper} /usr', hooks)
        manifest = self.prefix / 'share/doc/libencosdriver/PAYLOAD.sha256'
        self.assertIn('lib/encosPlugins/libEthercatIGHPlugin.so', manifest.read_text())
        self.assertIn('lib/encosPlugins/ethernet_xdp.bpf.o', manifest.read_text())


if __name__ == '__main__':
    unittest.main()
