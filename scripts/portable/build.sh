#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
output=${1:?Usage: build.sh ABSOLUTE_OUTPUT_DIRECTORY}
mkdir -p "$output"
output=$(cd "$output" && pwd)
if [[ $(getconf GNU_LIBC_VERSION) != 'glibc 2.28' ]]; then
    echo 'Portable release builds must use the glibc 2.28 CI image' >&2
    exit 1
fi
plugins='can;ethercat;usbSerial;slcan;fake;relayWs;ethercatIGH;ethernet'
node_args=(-DENCOS_BUILD_NODE_BINDINGS=OFF)
if [[ -n ${ENCOS_NODE_INCLUDE_DIR:-} ]]; then
    node_args=(-DENCOS_BUILD_NODE_BINDINGS=ON "-DENCOS_NODE_INCLUDE_DIR=$ENCOS_NODE_INCLUDE_DIR")
fi
cmake -S "$repo_root" -B "$output/build" -G "Unix Makefiles" \
    "${node_args[@]}" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_LIBDIR=lib -DENCOS_PLUGIN_INSTALL_LIBDIR=lib/encosPlugins \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DENCOS_PKEXEC_EXECUTABLE=/usr/bin/pkexec \
    -DENCOS_SETCAP_EXECUTABLE=/usr/sbin/setcap \
    -DENCOS_PORTABLE_LINUX=ON -DENCOS_STATIC_MODE=OFF \
    -DENCOS_ENABLE_INSTALL=ON -DENCOS_BUILD_TESTS=OFF \
    -DENCOS_PLUGINS_LIST="$plugins"
cmake --build "$output/build" -j"${ENCOS_BUILD_JOBS:-4}"
python3 "$repo_root/scripts/portable/stage-plugins.py" "$output" --reset
DESTDIR="$output/stage" cmake --install "$output/build"
prefix="$output/stage/usr"
mkdir -p "$prefix/share/doc/libencosdriver/third-party"
find /opt/encos-licenses -maxdepth 1 -type f -exec cp '{}' "$prefix/share/doc/libencosdriver/third-party/" \;
python3 "$repo_root/scripts/portable/stage-plugins.py" "$output"
python3 "$repo_root/scripts/portable/relink-kit.py" "$output"
relink="$output/stage/usr/share/doc/libencosdriver/relink/ethernet/relink.py"
python3 "$relink" helper "$output/relinked-ethernet-helper" --libelf /opt/encos-plugin-deps/lib/libelf.a
python3 "$relink" plugin "$output/relinked-ethernet-plugin.so" --core-prefix "$prefix" \
    --libelf /opt/encos-plugin-deps/lib/libelf.a
python3 "$repo_root/scripts/portable/bundle.py" "$prefix" \
    --library-directory /opt/encos-plugin-deps/lib --private-directory lib/encosPlugins/deps \
    > "$output/elf-audit.json"
# 消费安装后的 SDK；不把测试代码或测试插件放入发布包。
cmake -S "$repo_root/tests/portable" -B "$output/smoke-build" \
    -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$output/verification"
cmake --build "$output/smoke-build" -j2
cmake --install "$output/smoke-build"
LD_LIBRARY_PATH="$prefix/lib" "$output/verification/bin/encos-portable-smoke" "$prefix/lib/encosPlugins" EthercatIGH,Ethernet
# 验证工具单独传给测试矩阵，不进入 SDK 包。
cp "$repo_root/scripts/portable/bundle.py" "$output/verification/"
cp "$repo_root/tests/portable/smoke.cc" "$output/verification/"
cp "$repo_root/tests/portable/CMakeLists.txt" "$output/verification/"
