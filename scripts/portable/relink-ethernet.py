#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""使用随包目标文件和可替换的 libelf 静态库重新链接 Ethernet 组件。"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=('helper', 'plugin'))
    parser.add_argument('output', type=Path)
    parser.add_argument('--core-prefix', type=Path, default=Path('/usr'))
    parser.add_argument('--libelf', type=Path, help='替换 libelf.a 的路径')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    command = shlex.split(os.environ.get('CXX', 'c++'))
    for token in json.loads((root / (args.target + '.json')).read_text()):
        if token == '@OUTPUT@':
            token = str(args.output.resolve())
        elif token.startswith('@OBJECTS@/'):
            token = str(root / token.replace('@OBJECTS@/', 'objects/', 1))
            if args.libelf and Path(token).name == 'libelf.a':
                token = str(args.libelf.resolve())
        elif token.startswith('@CORE@/'):
            token = str(args.core_prefix / token[len('@CORE@/'):])
        command.append(token)
    subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
