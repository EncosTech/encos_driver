#!/usr/bin/env bash
set -euo pipefail

MODE="fix"
if [[ "${1:-}" == "--check" ]]; then
    MODE="check"
    shift
fi

mapfile -t FILES < <(
    find include src examples tests \
        \( -name '*.h' -o -name '*.c' \) \
        -type f \
        | sort
)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "No project source files found."
    exit 0
fi

if [[ "${MODE}" == "check" ]]; then
    clang-format --dry-run --Werror "${FILES[@]}"
else
    clang-format -i "${FILES[@]}"
fi
