#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""下载固定版本 Node 工具及头文件，并核对官方 SHA256 清单。"""
import argparse
import hashlib
import http.client
from pathlib import Path
import platform
import tarfile
import time
import urllib.request
import zipfile


def download(url, expected_sha256=None):
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                payload = response.read()
            if expected_sha256 and hashlib.sha256(payload).hexdigest() != expected_sha256:
                raise ValueError('Downloaded checksum mismatch')
            return payload
        except (OSError, http.client.HTTPException, ValueError) as error:
            if attempt == 3:
                raise
            print(f'Retrying {url.rsplit("/", 1)[-1]} ({attempt + 1}/3): {error}', flush=True)
            time.sleep(2 ** attempt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--version', default='22.23.4', choices=['22.23.4', '24.22.1'])
    parser.add_argument('--headers-only', action='store_true')
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    base = f'https://nodejs.org/dist/v{args.version}/'
    checksums = dict(line.split()[::-1] for line in download(base + 'SHASUMS256.txt').decode().splitlines())
    arch = {'x86_64': 'x64', 'AMD64': 'x64', 'aarch64': 'arm64', 'arm64': 'arm64'}[platform.machine()]
    system = 'win' if platform.system() == 'Windows' else 'linux'
    names = [f'node-v{args.version}-headers.tar.gz']
    if not args.headers_only:
        names.append(f'node-v{args.version}-{system}-{arch}.' + ('zip' if system == 'win' else 'tar.gz'))
    for name in names:
        archive = root / name
        if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != checksums[name]:
            archive.write_bytes(download(base + name, checksums[name]))
        if hashlib.sha256(archive.read_bytes()).hexdigest() != checksums[name]:
            raise RuntimeError(f'Checksum mismatch: {name}')
        if name.endswith('.zip'):
            with zipfile.ZipFile(archive) as source:
                for member in source.namelist():
                    if not (root / member).resolve().is_relative_to(root):
                        raise RuntimeError('Archive path escapes destination')
                source.extractall(root)
        else:
            with tarfile.open(archive) as source:
                for member in source:
                    destination = (root / member.name).resolve()
                    if not destination.is_relative_to(root):
                        raise RuntimeError('Archive path escapes destination')
                    if member.issym() or member.islnk():
                        parent = destination.parent if member.issym() else root
                        if not (parent / member.linkname).resolve().is_relative_to(root):
                            raise RuntimeError('Archive link escapes destination')
                source.extractall(root)
        print(f'Verified and extracted {name}')


if __name__ == '__main__':
    main()
