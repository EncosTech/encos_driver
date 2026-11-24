#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""校验候选包并发布同一份字节；默认仅输出发布计划。"""
import argparse
import base64
import hashlib
import os
from pathlib import Path
import re
import sys
import urllib.error
import urllib.request


ARCHITECTURES = ("amd64", "arm64")


def package_names(version, arch):
    stem = f"libencosdriver_{version}_linux-glibc2.28_{arch}"
    return [stem + suffix for suffix in (".deb", ".rpm", ".tar.gz")]


def candidate_names(version, arch):
    return {*package_names(version, arch), f"verification-{arch}.tar.gz", f"elf-audit-{arch}.json"}


def read_checksums(manifest):
    if manifest.is_symlink():
        raise ValueError("Checksum manifest must not be a symlink")
    checksums = {}
    for line in manifest.read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (?:\./)?([^/]+)", line)
        if not match:
            raise ValueError(f"Invalid checksum entry in {manifest.name}")
        checksum, name = match.groups()
        if name in checksums:
            raise ValueError(f"Invalid candidate file: {name}")
        checksums[name] = checksum
    return checksums


def verify_checksum(path, checksum):
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"Invalid candidate file: {path.name}")
    if hashlib.sha256(path.read_bytes()).hexdigest() != checksum:
        raise ValueError(f"Checksum mismatch: {path.name}")


def candidates(root):
    """先完整校验两个架构，避免校验失败后留下部分发布。"""
    result = []
    versions = set()
    for arch in ARCHITECTURES:
        directory = root / arch
        manifest = directory / f"SHA256SUMS-{arch}"
        files = {}
        for name, checksum in read_checksums(manifest).items():
            path = directory / name
            verify_checksum(path, checksum)
            files[name] = path
        debs = [name for name in files if name.startswith("libencosdriver_") and name.endswith(".deb")]
        if len(debs) != 1:
            raise ValueError(f"Expected exactly one DEB for {arch}")
        match = re.fullmatch(r"libencosdriver_([0-9]+(?:\.[0-9]+)*)_linux-glibc2\.28_" + arch + r"\.deb", debs[0])
        if not match:
            raise ValueError(f"Unexpected package identity: {debs[0]}")
        version = match[1]
        versions.add(version)
        expected = candidate_names(version, arch)
        if set(files) != expected:
            raise ValueError(f"Incomplete or unexpected candidate set for {arch}")
        if {p.name for p in directory.iterdir()} != expected | {manifest.name}:
            raise ValueError(f"Unmanifested files in {directory}")
        deb, rpm, _ = package_names(version, arch)
        for distro in ("jammy", "noble"):
            result.append((files[deb], f"debian/pool/{distro}/main/upload"))
        result.append((files[rpm], "rpm/portable/upload"))
        # 通用仓库保存全部原件，便于下载与 SHA256SUMS 完整复验。
        for path in [*files.values(), manifest]:
            result.append((path, f"generic/encos-driver-linux/{version}/{path.name}"))
    if len(versions) != 1:
        raise ValueError("Architecture candidates have different versions")
    return result


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate_directory", type=Path)
    parser.add_argument("--publish", action="store_true", help="执行上传；默认只校验并显示计划")
    args = parser.parse_args()
    plan = candidates(args.candidate_directory)
    if not args.publish:
        for path, endpoint in plan:
            print(f"Verified: {path.name} -> {endpoint}")
        return
    secret = os.environ.get("USER_PASSWORD")
    if not secret:
        raise ValueError("USER_PASSWORD is required")
    username = os.environ.get("GITEA_PACKAGE_USER", "bismarckkk")
    authorization = base64.b64encode(f"{username}:{secret}".encode()).decode()
    opener = urllib.request.build_opener(NoRedirect)
    for path, endpoint in plan:
        request = urllib.request.Request(
            "https://git.in.encos.cn/api/packages/packages/" + endpoint,
            data=path.read_bytes(), method="PUT",
            headers={"Authorization": "Basic " + authorization,
                     "Content-Type": "application/octet-stream"},
        )
        try:
            with opener.open(request, timeout=300) as response:
                if response.status not in (200, 201, 204):
                    raise ValueError(f"Upload failed: {path.name}, HTTP {response.status}")
        except urllib.error.HTTPError as error:
            raise ValueError(f"Upload failed: {path.name}, HTTP {error.code}") from None
        except urllib.error.URLError:
            raise ValueError(f"Upload transport failed: {path.name}") from None
        print(f"Published: {path.name} -> {endpoint}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        print(f"Publication aborted: {error}", file=sys.stderr)
        sys.exit(1)
