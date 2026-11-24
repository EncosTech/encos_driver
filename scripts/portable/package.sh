#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
output=${1:?Usage: package.sh BASELINE_OUTPUT_DIRECTORY}
output=$(cd "$output" && pwd)
for tool in cpack dpkg-deb rpmbuild readelf; do command -v "$tool" >/dev/null; done
python3 "$repo_root/scripts/portable/bundle.py" "$output/stage/usr" --audit-only --private-directory lib/encosPlugins/deps > "$output/elf-audit.json"
python3 "$repo_root/scripts/portable/package-config.py" "$repo_root" "$output"
# 仅清理本脚本生成的输出，防止旧版拆分包进入候选清单。
rm -rf -- "$output/packages"
cpack --config "$output/PortableCPackConfig.cmake"
rm -rf -- "$output/packages/_CPack_Packages"
arch=$(cat "$output/architecture")
tar -czf "$output/packages/verification-${arch}.tar.gz" -C "$output/verification" .
cp "$output/elf-audit.json" "$output/packages/elf-audit-${arch}.json"
(cd "$output/packages" && sha256sum ./*.deb ./*.rpm ./*.tar.gz ./*.json > "SHA256SUMS-${arch}")
