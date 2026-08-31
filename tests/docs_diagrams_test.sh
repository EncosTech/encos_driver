#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_root=$(mktemp -d)
trap 'rm -rf "${test_root}"' EXIT

rsvg-convert \
    "${repo_root}/docs/diagrams/motor_control_modes_overview.svg" \
    --format pdf \
    --output "${test_root}/motor_control_modes_overview.pdf"
pdftotext "${test_root}/motor_control_modes_overview.pdf" \
    "${test_root}/motor_control_modes_overview.txt"
tr -d '[:space:]' <"${test_root}/motor_control_modes_overview.txt" \
    >"${test_root}/motor_control_modes_overview.normalized.txt"

grep -Fq "用户发起控制指令" \
    "${test_root}/motor_control_modes_overview.normalized.txt"
grep -Fq "状态由SetOnStatus回调异步递送" \
    "${test_root}/motor_control_modes_overview.normalized.txt"

printf 'docs diagram rendering test passed\n'
