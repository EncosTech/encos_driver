#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""验证完整安装树并生成统一 Linux 软件包元数据。"""
import hashlib
from pathlib import Path
import platform
import re
import subprocess
import sys

PLUGINS = {"Can", "Ethercat", "UsbSerial", "Slcan", "Fake", "RelayWs", "EthercatIGH", "Ethernet"}
LICENSE = "MIT AND GPL-3.0-or-later AND GPL-2.0-only AND LGPL-2.1-only AND LGPL-3.0-or-later AND BSD-2-Clause AND BSD-3-Clause AND Zlib"
HELPERS = ("set_socketcan_capabilities.sh", "set_ethercat_capabilities.sh",
           "set_thread_priority_capability.sh", "set_ethernet_capabilities.sh")
BASE_FILES = ("include/encos/encos/encos_driver.h", "lib/libencosdriver.so", "lib/libencosdriver_base.so",
              "lib/cmake/encos_driver/encos_driverConfig.cmake",
              "lib/encosPlugins/CanFdBrokerExecutable", "lib/encosPlugins/EthercatFdBrokerExecutable",
              "lib/encosPlugins/ThreadPriorityHelper")
HEADER = "#!/bin/sh\n# SPDX-FileCopyrightText: 2026 Encos\n# SPDX-License-Identifier: MIT\n"


def validate(prefix):
    if not prefix.is_dir():
        raise ValueError(f"Missing package stage: {prefix}")
    staged_paths = list(prefix.rglob("*"))
    paths = {p.relative_to(prefix).as_posix() for p in staged_paths
             if p.is_file() or p.is_symlink()}
    expected = {f"lib{kind}Plugin.so" for kind in PLUGINS}
    actual = {p.name for p in (prefix / "lib/encosPlugins").glob("*Plugin.so")}
    if actual != expected or any(not (prefix / "lib/encosPlugins" / name).is_file() for name in expected):
        raise ValueError(f"Unexpected plugins: {actual}; expected {expected}")
    for required in BASE_FILES:
        if required not in paths:
            raise ValueError(f"Missing SDK file: {required}")
    for helper in (*HELPERS, "EthernetXdpBrokerExecutable", "ethernet_xdp.bpf.o"):
        if f"lib/encosPlugins/{helper}" not in paths:
            raise ValueError(f"Missing runtime helper: {helper}")
    if any(p.relative_to(prefix).as_posix().startswith("share/doc/libencosdriver-plugin-")
           for p in staged_paths):
        raise ValueError("Obsolete split package documentation")
    doc = prefix / "share/doc/libencosdriver"
    if (doc / "third-party-sources").exists():
        raise ValueError("Source archives must not be included in the runtime package")
    for relative in ("relink/ethernet",):
        directory = doc / relative
        if not directory.is_dir() or not any(p.is_file() for p in directory.rglob("*")):
            raise ValueError(f"Missing relink material: {relative}")


def generate(repo, output):
    prefix = output / "stage/usr"
    validate(prefix)
    version = re.search(r'project\(encos_driver VERSION ([0-9.]+)\)',
                        (repo / 'CMakeLists.txt').read_text())[1]
    arch = {'x86_64': 'amd64', 'aarch64': 'arm64'}[platform.machine()]
    (output / 'architecture').write_text(arch + '\n')
    name = 'libencosdriver'
    manifest = prefix / f'share/doc/{name}/PAYLOAD.sha256'
    manifest.parent.mkdir(parents=True, exist_ok=True)
    files = sorted(p for p in prefix.rglob('*') if p.is_file() and not p.is_symlink() and p != manifest)
    manifest.write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(prefix)}\n' for p in files))
    postinst, postrm = output / 'postinst', output / 'postrm'
    hooks = ''.join(f'bash /usr/lib/encosPlugins/{helper} /usr || echo "Warning: configure capabilities with {helper}" >&2\n'
                    for helper in HELPERS)
    ldconfig = 'if command -v ldconfig >/dev/null 2>&1; then ldconfig; fi\n'
    postinst.write_text(HEADER + hooks + ldconfig)
    postrm.write_text(HEADER + ldconfig)
    postinst.chmod(0o755)
    postrm.chmod(0o755)
    deb_requires = 'libc6 (>= 2.28), libstdc++6 (>= 8), libgcc-s1 | libgcc1, libsystemd0, polkitd'
    rpm_requires = 'glibc >= 2.28, libstdc++ >= 8, libgcc, systemd-libs, polkit'
    provides = []
    for library in sorted((prefix / 'lib').glob('libencosdriver*.so.*')):
        if library.is_symlink():
            continue
        dynamic = subprocess.check_output(['readelf', '-d', str(library)], text=True)
        soname = re.search(r'\(SONAME\).*\[(.+?)\]', dynamic)
        if soname:
            provides.append(soname[1] + '()(64bit)')
    config = f'''# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
set(CPACK_GENERATOR "TGZ;DEB;RPM")
set(CPACK_PACKAGE_NAME "{name}")
set(CPACK_PACKAGE_VERSION "{version}")
set(CPACK_PACKAGE_CONTACT "Encos <Sales002@encos.cn>")
set(CPACK_PACKAGE_VENDOR "Encos")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "EncosDriver portable glibc 2.28 SDK and all Linux adapters")
set(CPACK_PACKAGE_DESCRIPTION "C++17 motor driver SDK and all eight Linux adapter plugins")
set(CPACK_PACKAGE_FILE_NAME "{name}_{version}_linux-glibc2.28_{arch}")
set(CPACK_PACKAGE_DIRECTORY "{output}/packages")
set(CPACK_INSTALLED_DIRECTORIES "{prefix.parent};/")
set(CPACK_PACKAGING_INSTALL_PREFIX "/")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "{arch}")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "{deb_requires}")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libcap2-bin")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "pkexec, iproute2")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "{postinst};{postrm}")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
set(CPACK_RPM_PACKAGE_ARCHITECTURE "{platform.machine()}")
set(CPACK_RPM_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_LICENSE "{LICENSE}")
set(CPACK_RPM_PACKAGE_AUTOREQPROV OFF)
# RPM 不重写已审计的 ELF，也不注册私有依赖原有的系统 build-id。
set(CPACK_RPM_SPEC_MORE_DEFINE "%define __os_install_post %{{nil}}\\n%define _build_id_links none")
set(CPACK_RPM_PACKAGE_REQUIRES "{rpm_requires}")
set(CPACK_RPM_PACKAGE_RECOMMENDS "libcap")
set(CPACK_RPM_PACKAGE_SUGGESTS "iproute")
set(CPACK_RPM_PACKAGE_PROVIDES "{', '.join(sorted(set(provides)))}")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "{postinst}")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "{postrm}")
'''
    # DEB control scripts must have the canonical basename.
    control = output / 'control'
    control.mkdir(exist_ok=True)
    for source, basename in ((postinst, 'postinst'), (postrm, 'postrm')):
        (control / basename).write_bytes(source.read_bytes())
        (control / basename).chmod(0o755)
    config = config.replace(f'{postinst};{postrm}', f'{control}/postinst;{control}/postrm')
    (output / 'PortableCPackConfig.cmake').write_text(config)


if __name__ == '__main__':
    generate(*map(Path, sys.argv[1:]))
