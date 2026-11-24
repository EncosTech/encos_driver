#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""发布入口的离线回归测试，不向任何仓库上传。"""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "portable_publish", Path(__file__).resolve().parents[1] / "portable/publish.py"
)
publish = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publish)


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for arch in ("amd64", "arm64"):
            self.create_candidate(arch)

    def create_candidate(self, arch, version="1.2.3"):
        directory = self.root / arch
        directory.mkdir(exist_ok=True)
        for path in directory.iterdir():
            path.unlink()
        names = [f"libencosdriver_{version}_linux-glibc2.28_{arch}{suffix}"
                 for suffix in (".deb", ".rpm", ".tar.gz")]
        names += [f"verification-{arch}.tar.gz", f"elf-audit-{arch}.json"]
        lines = []
        for name in names:
            data = name.encode()
            (directory / name).write_bytes(data)
            lines.append(f"{hashlib.sha256(data).hexdigest()}  ./{name}\n")
        (directory / f"SHA256SUMS-{arch}").write_text("".join(lines))

    def test_same_deb_used_for_both_indexes(self):
        plan = publish.candidates(self.root)
        for arch in ("amd64", "arm64"):
            debs = [(path, endpoint) for path, endpoint in plan
                    if path.parent.name == arch and endpoint.startswith("debian/")]
            self.assertEqual(len(debs), 2)
            for path in {path for path, _ in debs}:
                self.assertEqual(sum(candidate == path for candidate, _ in debs), 2)
        self.assertEqual(len(plan), 18)

    def test_tamper_aborts_before_upload(self):
        next((self.root / "arm64").glob("*.rpm")).write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            publish.candidates(self.root)

    def test_cross_arch_version_mismatch(self):
        self.create_candidate("arm64", "1.2.4")
        with self.assertRaisesRegex(ValueError, "different versions"):
            publish.candidates(self.root)

    def test_unmanifested_files_are_rejected(self):
        (self.root / "amd64" / "unexpected.deb").write_bytes(b"extra")
        with self.assertRaisesRegex(ValueError, "Unmanifested"):
            publish.candidates(self.root)

    def test_missing_unified_package_rejected(self):
        directory = self.root / "amd64"
        name = "libencosdriver_1.2.3_linux-glibc2.28_amd64.rpm"
        (directory / name).unlink()
        manifest = directory / "SHA256SUMS-amd64"
        manifest.write_text("".join(line for line in manifest.read_text().splitlines(True)
                                    if name not in line))
        with self.assertRaisesRegex(ValueError, "candidate set"):
            publish.candidates(self.root)

    def test_old_split_artifacts_rejected_even_if_manifested(self):
        for name in ('libencosdriver-plugin-igh_1.2.3_linux-glibc2.28_amd64.deb',
                     'libencosdriver-plugin-ethernet_1.2.3_linux-glibc2.28_amd64.rpm',
                     'elf-audit-igh-amd64.json'):
            with self.subTest(name=name):
                self.create_candidate('amd64')
                directory = self.root / 'amd64'
                (directory / name).write_bytes(b'old split package')
                manifest = directory / 'SHA256SUMS-amd64'
                with manifest.open('a') as output:
                    output.write(f"{hashlib.sha256(b'old split package').hexdigest()}  {name}\n")
                with self.assertRaisesRegex(ValueError, 'candidate set'):
                    publish.candidates(self.root)

    def test_exact_six_files_per_architecture(self):
        for arch in ('amd64', 'arm64'):
            directory = self.root / arch
            self.assertEqual(len(list(directory.iterdir())), 6)
            self.assertEqual(len((directory / f'SHA256SUMS-{arch}').read_text().splitlines()), 5)

    def test_path_traversal_is_rejected(self):
        manifest = self.root / "amd64" / "SHA256SUMS-amd64"
        manifest.write_text("0" * 64 + "  ../outside.deb\n")
        with self.assertRaisesRegex(ValueError, "Invalid checksum"):
            publish.candidates(self.root)


if __name__ == "__main__":
    unittest.main()
