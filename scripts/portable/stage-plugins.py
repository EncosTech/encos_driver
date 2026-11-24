#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""为包含全部插件的安装树整理第三方许可和构建信息。"""
import argparse
from pathlib import Path
import shutil
import tarfile

def copy_xdp_bpf_license(archive, destination):
    """只读取匹配的普通归档成员，不向文件系统展开归档路径。"""
    with tarfile.open(archive) as source:
        matches = [member for member in source.getmembers()
                   if member.isfile() and member.name.endswith('/LICENSES/GPL-2.0')]
        if len(matches) != 1:
            raise RuntimeError('Expected one xdp-tools LICENSES/GPL-2.0 regular file')
        with source.extractfile(matches[0]) as license_file:
            destination.write_bytes(license_file.read())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--reset', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    if args.reset:
        for name in ('stage', 'stage-igh', 'stage-ethernet', 'verification'):
            path = output / name
            if path.is_symlink():
                raise RuntimeError(f'Refusing symlink stage: {path}')
            if path.exists():
                shutil.rmtree(path)
        return
    doc = output / 'stage/usr/share/doc/libencosdriver'
    licenses = doc / 'third-party'
    # 新镜像中的 libcap 许可覆盖基础镜像中旧依赖的许可。
    shutil.copytree('/opt/encos-licenses/plugins', licenses, dirs_exist_ok=True)
    sources = Path('/opt/encos-plugin-sources')
    if not sources.is_dir():
        raise RuntimeError('Missing corresponding third-party source archives')
    # 源码由独立公开仓库提供，安装包只保留版本和校验信息。
    shutil.copy2(sources / 'VERSIONS', doc / 'dependency-versions.txt')
    copy_xdp_bpf_license(sources / 'xdp-tools.tar', licenses / 'libxdp-BPF-GPL2.txt')
    for name in ('source-sha256sums.txt', 'build-rpms.txt'):
        source = Path('/opt/encos-plugin-deps') / name
        if source.is_file():
            shutil.copy2(source, doc / name)


if __name__ == '__main__':
    main()
