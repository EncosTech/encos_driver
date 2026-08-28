#!/usr/bin/env bash
set -euo pipefail

# 判断主 CMakeLists.txt 中的项目版本号是否发生变化。
# 用于 CI 中在测试通过后决定是否继续执行 release/build 步骤。
#
# 输入：
#   - 环境变量 GITHUB_EVENT_NAME：当前触发事件名称
#   - 环境变量 GITHUB_EVENT_JSON：当前事件 JSON（由 toJson(github.event) 提供）
#   - 代码需已通过 actions/checkout 检出（且包含待比较提交历史）
# 输出：
#   - 写入 GITHUB_OUTPUT：should_run=true/false

if [ -z "${GITHUB_EVENT_NAME:-}" ]; then
  echo "GITHUB_EVENT_NAME is not set" >&2
  exit 1
fi

event_json="${GITHUB_EVENT_JSON:-}"
if [ -z "$event_json" ]; then
  event_json=$(cat)
fi

should_run=$(python3 - "$GITHUB_EVENT_NAME" "$event_json" <<'PY'
import json
import re
import subprocess
import sys
from pathlib import Path

event_name = sys.argv[1]
event = json.loads(sys.argv[2] or "{}")
project_name = "encos_driver"
project_file = Path("CMakeLists.txt")


def extract_version(content):
    pattern = re.compile(
        r"project\s*\(\s*" + re.escape(project_name) + r"\s+VERSION\s+([0-9]+(?:\.[0-9]+)*)"
    )
    match = pattern.search(content)
    return match.group(1) if match else None


def git_show(ref, path):
    return subprocess.check_output(
        ["git", "show", f"{ref}:{path}"],
        text=True,
        stderr=subprocess.DEVNULL,
    )


def versions_differ(base_ref):
    try:
        head_version = extract_version(project_file.read_text())
        base_version = extract_version(git_show(base_ref, project_file.as_posix()))
    except (OSError, subprocess.CalledProcessError):
        return False
    return bool(head_version and base_version and head_version != base_version)


if event_name == "workflow_dispatch":
    print("true")
    sys.exit(0)

if event_name == "pull_request":
    print("false")
    sys.exit(0)

if event_name == "push":
    before = event.get("before") or "HEAD^"
    if set(before) == {"0"}:
        before = "HEAD^"
    print("true" if versions_differ(before) else "false")
    sys.exit(0)

workflow_run = event.get("workflow_run", {})
if event_name == "workflow_run":
    if workflow_run.get("conclusion") != "success" or workflow_run.get("event") != "push":
        print("false")
        sys.exit(0)
    print("true" if versions_differ("HEAD^") else "false")
    sys.exit(0)

print("false")
PY
)

if [ -n "${GITHUB_OUTPUT:-}" ]; then
  echo "should_run=$should_run" >> "$GITHUB_OUTPUT"
else
  echo "should_run=$should_run"
fi
