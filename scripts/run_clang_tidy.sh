#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"
shift || true

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
    echo "compile_commands.json not found under ${BUILD_DIR}" >&2
    echo "Configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON first." >&2
    exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

mapfile -t FILES < <(
    python3 - "${BUILD_DIR}/compile_commands.json" "${ROOT_DIR}" <<'PY'
import json
import pathlib
import sys

compile_commands = pathlib.Path(sys.argv[1])
root_dir = pathlib.Path(sys.argv[2]).resolve()

with compile_commands.open() as f:
    entries = json.load(f)

seen = set()
files = []
for entry in entries:
    file_path = pathlib.Path(entry["file"]).resolve()
    if file_path.suffix != ".cc":
        continue
    try:
        rel = file_path.relative_to(root_dir)
    except ValueError:
        continue
    rel_str = rel.as_posix()
    if not (
        rel_str.startswith("src/")
        or rel_str.startswith("plugins/")
        or rel_str.startswith("tests/")
    ):
        continue
    if rel_str not in seen:
        seen.add(rel_str)
        files.append(rel_str)

for path in sorted(files):
    print(path)
PY
)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "No translation units found."
    exit 0
fi

for file in "${FILES[@]}"; do
    echo "Running clang-tidy: ${file}"
    clang-tidy -p "${BUILD_DIR}" "${file}" "$@"
done
