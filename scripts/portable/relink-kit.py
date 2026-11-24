#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""保存 Ethernet 链接输入，使用户可以替换 LGPL 静态库后重新链接。"""
import json
from pathlib import Path
import shlex
import shutil
import sys


def main():
    output = Path(sys.argv[1]).resolve()
    root = Path(__file__).resolve().parent
    destination = output / 'stage/usr/share/doc/libencosdriver/relink/ethernet'
    objects = destination / 'objects'
    objects.mkdir(parents=True, exist_ok=True)
    for name, target in (('helper', 'EthernetXdpBrokerExecutable'), ('plugin', 'EthernetPlugin')):
        matches = list((output / 'build').rglob(f'{target}.dir/link.txt'))
        if len(matches) != 1:
            raise RuntimeError(f'Cannot find link recipe for {target}')
        recipe = matches[0]
        directory = recipe.parent.parent.parent
        command = shlex.split(recipe.read_text())[1:]
        result = []
        index = 0
        while index < len(command):
            token = command[index]
            index += 1
            if token == '-o':
                result += ['-o', '@OUTPUT@']
                index += 1
                continue
            if token.startswith('-Wl,--dependency-file=') or token.startswith('-Wl,-rpath,'):
                continue
            path = Path(token)
            if not path.is_absolute():
                path = directory / path
            if token.endswith(('.a', '.o')) and path.is_file():
                source = path.resolve()
                target_path = objects / source.name
                if target_path.exists() and target_path.read_bytes() != source.read_bytes():
                    raise RuntimeError(f'Conflicting relink input: {source.name}')
                shutil.copy2(source, target_path)
                token = '@OBJECTS@/' + source.name
            elif path.name.startswith('libencosdriver') and path.is_file():
                token = '@CORE@/lib/' + path.name
            result.append(token)
        if name == 'plugin':
            result.append('-Wl,-rpath,$ORIGIN/..:$ORIGIN/deps')
        (destination / f'{name}.json').write_text(json.dumps(result, indent=2) + '\n')
    shutil.copy2(root / 'relink-ethernet.py', destination / 'relink.py')
    (destination / 'README.txt').write_text(
        'Use the glibc 2.28 baseline compiler (GCC toolset 14) and matching core SDK.\n'
        'All application objects and static dependencies needed for relinking are in objects/.\n'
        'Dependency sources are distributed separately, not included in this package.\n'
        'Versions and source archive checksums: ../../dependency-versions.txt and ../../source-sha256sums.txt.\n'
        'Build a modified PIC libelf.a from the corresponding source, then run:\n'
        'python3 relink.py helper ./EthernetXdpBrokerExecutable --libelf /path/to/libelf.a\n'
        'python3 relink.py plugin ./libEthernetPlugin.so --libelf /path/to/libelf.a --core-prefix /usr\n'
        'CXX selects the compiler. Install the rebuilt files using the package paths,\n'
        'then reapply the helper capabilities with set_ethernet_capabilities.sh.\n')


if __name__ == '__main__':
    main()
