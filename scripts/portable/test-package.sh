#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
set -euo pipefail
packages=${1:?Usage: test-package.sh CANDIDATE_DIRECTORY [deb|rpm|tgz]}
format=${2:-deb}
packages=$(cd "$packages" && pwd)
machine=$(uname -m)
case "$machine" in x86_64) arch=amd64;; aarch64) arch=arm64;; *) exit 1;; esac
(cd "$packages" && sha256sum -c "SHA256SUMS-${arch}")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
as_root=()
if [[ $EUID -ne 0 ]]; then as_root=(sudo); fi
suffix=$format
if [[ $format == tgz ]]; then suffix=tar.gz; fi
shopt -s nullglob
package=("$packages"/libencosdriver_*.$suffix)
[[ ${#package[@]} == 1 ]]
tar -xzf "$packages/verification-${arch}.tar.gz" -C "$work"
case "$format" in
    deb)
        [[ $(dpkg-deb -f "${package[0]}" Package) == libencosdriver ]]
        [[ $(dpkg-deb -f "${package[0]}" Architecture) == "$arch" ]]
        [[ $(dpkg-deb -f "${package[0]}" Recommends) == libcap2-bin ]]
        [[ $(dpkg-deb -f "${package[0]}" Suggests) == 'pkexec, iproute2' ]]
        dependencies=$(dpkg-deb -f "${package[0]}" Depends Pre-Depends Recommends Suggests)
        runtime=$(dpkg-deb -f "${package[0]}" Depends)
        [[ $runtime == *libsystemd0* && $runtime == *polkitd* ]]
        "${as_root[@]}" dpkg --path-include='/usr/share/doc/*' -i "${package[0]}"; prefix=/usr ;;
    rpm)
        [[ $(rpm -qp --qf '%{NAME}' "${package[0]}") == libencosdriver ]]
        [[ $(rpm -qp --qf '%{ARCH}' "${package[0]}") == "$machine" ]]
        [[ $(rpm -qp --recommends "${package[0]}") == libcap ]]
        [[ $(rpm -qp --suggests "${package[0]}" | sort) == iproute ]]
        runtime=$(rpm -qp --requires "${package[0]}")
        grep -Fxq systemd-libs <<<"$runtime"
        grep -Fxq polkit <<<"$runtime"
        dependencies=$(
            rpm -qp --requires "${package[0]}"
            rpm -qp --recommends "${package[0]}"
            rpm -qp --suggests "${package[0]}"
        )
        "${as_root[@]}" rpm -Uvh "${package[0]}"; prefix=/usr ;;
    tgz) mkdir -p "$work/relocated"; tar -xzf "${package[0]}" -C "$work/relocated"; prefix="$work/relocated/usr"; dependencies='' ;;
    *) echo "Unknown package format: $format" >&2; exit 1 ;;
esac
if [[ ${dependencies,,} == *networkmanager* || ${dependencies,,} == *network-manager* ||
      ${dependencies,,} == *libnm* ]]; then
    echo 'Package metadata must not declare a NetworkManager dependency' >&2
    exit 1
fi
if [[ ${dependencies,,} == *igh* || ${dependencies,,} == *ethercat* ]]; then
    echo 'Package metadata must not declare an IGH dependency' >&2
    exit 1
fi
(cd "$prefix" && sha256sum -c share/doc/libencosdriver/PAYLOAD.sha256)
[[ ! -e $prefix/share/doc/libencosdriver/third-party-sources ]]
[[ -s $prefix/share/doc/libencosdriver/dependency-versions.txt ]]
[[ -s $prefix/share/doc/libencosdriver/source-sha256sums.txt ]]
[[ -f $prefix/share/doc/libencosdriver/relink/ethernet/relink.py ]]
LD_LIBRARY_PATH="$prefix/lib" "$work/bin/encos-portable-smoke" "$prefix/lib/encosPlugins" 'EthercatIGH,Ethernet'
# 记录包拥有的普通文件及符号链接，卸载后逐项确认清理完成。
case "$format" in
    deb) dpkg-query -L libencosdriver > "$work/installed-paths" ;;
    rpm) rpm -ql libencosdriver > "$work/installed-paths" ;;
    tgz) find "$prefix" \( -type f -o -type l \) > "$work/installed-paths" ;;
esac
while IFS= read -r path; do
    if [[ -f $path || -L $path ]]; then printf '%s\n' "$path"; fi
done < "$work/installed-paths" > "$work/owned-files"
[[ -s $work/owned-files ]]
# 在消费方重新编译探针：验证安装后的 CMake SDK 无需系统 fmt/spdlog 开发包。
if [[ ${ENCOS_REQUIRE_SDK_TEST:-0} == 1 ]]; then
    command -v cmake >/dev/null
    command -v c++ >/dev/null
fi
if command -v cmake >/dev/null && command -v c++ >/dev/null; then
    cmake -S "$work" -B "$work/consumer" -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$work/consumer" -j2
    LD_LIBRARY_PATH="$prefix/lib" "$work/consumer/encos-portable-smoke" "$prefix/lib/encosPlugins" 'EthercatIGH,Ethernet'
fi
# 在没有驱动共享库的目录中，以普通用户执行带 capability 的 broker。
if [[ ${ENCOS_REQUIRE_CAPABILITY_TEST:-0} == 1 ]]; then
    command -v setcap >/dev/null
    command -v runuser >/dev/null
    chmod 755 "$work"
    for helper in CanFdBrokerExecutable EthercatFdBrokerExecutable EthernetXdpBrokerExecutable; do
        expected_status=1
        if [[ $helper == EthernetXdpBrokerExecutable ]]; then expected_status=2; fi
        "${as_root[@]}" install -m755 "$prefix/lib/encosPlugins/$helper" "$work/$helper"
        "${as_root[@]}" setcap cap_net_raw=ep "$work/$helper"
        status=0
        "${as_root[@]}" runuser -u nobody -- "$work/$helper" || status=$?
        if [[ $status != "$expected_status" ]]; then
            echo "Privileged helper $helper failed startup: $status" >&2
            exit 1
        fi
    done
fi
case "$format" in
    deb) "${as_root[@]}" dpkg --purge libencosdriver ;;
    rpm) "${as_root[@]}" rpm -e libencosdriver ;;
    tgz) rm -rf "$work/relocated" ;;
esac
while IFS= read -r path; do
    if [[ -e $path || -L $path ]]; then
        echo "Package payload remains after uninstall: $path" >&2
        exit 1
    fi
done < "$work/owned-files"
printf 'PASS: %s %s on %s\n' "$format" "$arch" "$(cat /etc/os-release | tr '\n' ' ')"
