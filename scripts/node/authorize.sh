#!/bin/bash
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
set -euo pipefail
if [ "$#" -ne 0 ]; then
    echo 'This script does not accept arguments.' >&2
    exit 2
fi
package_root="$(cd -- "$(dirname -- "$(readlink -f -- "$0")")/.." && pwd -P)"
runtime="$package_root/runtime"
helper_dir="$runtime/lib/encosPlugins"
for script in set_socketcan_capabilities.sh set_ethercat_capabilities.sh set_ethernet_capabilities.sh set_thread_priority_capability.sh; do
    if [ ! -f "$helper_dir/$script" ]; then
        echo "Missing bundled authorization script: $helper_dir/$script" >&2
        exit 1
    fi
done
for script in set_socketcan_capabilities.sh set_ethercat_capabilities.sh set_ethernet_capabilities.sh set_thread_priority_capability.sh; do
    /bin/bash "$helper_dir/$script" "$runtime"
done
