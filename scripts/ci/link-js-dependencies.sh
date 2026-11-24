#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
lock_record=/opt/encos-js/pnpm-lock.sha256
if [[ ! -r $lock_record ]]; then
    echo 'CI image has no JavaScript dependency inventory; rebuild emd_container CI images.' >&2
    exit 1
fi
read -r expected _ < "$lock_record"
actual=$(sha256sum "$repo_root/npm/pnpm-lock.yaml")
if [[ ${actual%% *} != "$expected" ]]; then
    echo 'npm lockfile differs from the CI image; rebuild emd_container CI images with this lockfile.' >&2
    exit 1
fi
pnpm --dir "$repo_root/npm" install --offline --frozen-lockfile --store-dir /opt/encos-pnpm-store
