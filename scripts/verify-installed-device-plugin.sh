#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

#
# 端到端验证：在仅安装 DEB（无驱动源码）的环境中编写并加载外部设备插件。
#
#   1. 以 ENCOS_BUILD_TESTS=OFF 打包构建驱动并生成 DEB
#   2. 解包 DEB 得到安装前缀（不执行安装脚本，无需 root）
#   3. 用 find_package(encos_driver) + add_emd_plugin 构建样例设备插件并安装
#   4. 构建运行期探针
#   5. 运行探针，验证插件发现、设备创建与配置身份语义
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work_dir=${ENCOS_VERIFY_DIR:-"${repo_root}/build-installed-plugin-verify"}
fixture_dir="${repo_root}/tests/cmake/installed-device-plugin-project"

rm -rf "${work_dir}"
mkdir -p "${work_dir}"

printf '== 1/5 打包构建（ENCOS_BUILD_TESTS=OFF）\n'
cmake -S "${repo_root}" -B "${work_dir}/package-build" \
    -DENCOS_BUILD_TESTS=OFF -DENCOS_ENABLE_INSTALL=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "${work_dir}/package-build" -j"$(nproc)" >/dev/null
(cd "${work_dir}/package-build" && cpack >/dev/null)
deb=$(ls "${work_dir}/package-build"/*.deb | head -1)
printf '   DEB: %s\n' "$(basename "${deb}")"

printf '== 2/5 解包 DEB\n'
mkdir -p "${work_dir}/deb"
dpkg-deb -x "${deb}" "${work_dir}/deb"
prefix="${work_dir}/deb/usr/local"

printf '== 3/5 find_package + add_emd_plugin 构建并安装外部设备插件\n'
cmake -S "${fixture_dir}" -B "${work_dir}/consumer-build" \
    -DCMAKE_PREFIX_PATH="${prefix}" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "${work_dir}/consumer-build" -j"$(nproc)" >/dev/null
plugin_so="${work_dir}/consumer-build/plugins/libHelloDevice.so"
if [[ ! -f "${plugin_so}" ]]; then
    printf '缺少产物 %s\n' "${plugin_so}" >&2
    exit 1
fi
cmake --install "${work_dir}/consumer-build" >/dev/null
printf '   产物: %s\n' "$(basename "${plugin_so}")"

printf '== 4/5 构建运行期探针\n'
cmake -S "${fixture_dir}/probe-project" -B "${work_dir}/probe-build" \
    -DCMAKE_PREFIX_PATH="${prefix}" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "${work_dir}/probe-build" -j"$(nproc)" >/dev/null

printf '== 5/5 运行期加载\n'
LD_LIBRARY_PATH="${prefix}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
    "${work_dir}/probe-build/probe" "${prefix}/lib/encosPlugins"

printf '端到端验证通过\n'
