#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""从 Gitea 通用仓库下载并校验 GitHub 发布包。"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from publish import ARCHITECTURES, candidate_names, package_names, read_checksums, verify_checksum


def download(repository, path):
    result = subprocess.run([
        "curl", "--fail-with-body", "--silent", "--show-error", "--location",
        "--proto-redir", "=https", "--header", f"Authorization: Basic {os.environ['GITEA_RELEASE_AUTH']}",
        "--output", str(path), f"{repository}/{path.name}",
    ])
    if result.returncode:
        raise ValueError(f"Download failed for {path.name}")


def prepare(repository, version, destination):
    checksums = {}
    with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
        for arch in ARCHITECTURES:
            manifest = Path(temporary) / f"SHA256SUMS-{arch}"
            download(repository, manifest)
            entries = read_checksums(manifest)
            if set(entries) != candidate_names(version, arch):
                raise ValueError(f"Unexpected candidate set in {manifest.name} for version {version}")
            for name in package_names(version, arch):
                checksums[name] = entries[name]

    for name, checksum in checksums.items():
        path = destination / name
        download(repository, path)
        verify_checksum(path, checksum)
    (destination / "SHA256SUMS").write_text(
        "".join(f"{checksum}  {name}\n" for name, checksum in sorted(checksums.items()))
    )


if __name__ == "__main__":
    try:
        repository, version, destination = sys.argv[1:]
        prepare(repository, version, Path(destination))
    except (KeyError, OSError, ValueError) as error:
        print(f"Portable release preparation aborted: {error}", file=sys.stderr)
        sys.exit(1)
