#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
set -euo pipefail
set +x
: "${USER_PASSWORD:?USER_PASSWORD is required for private dependencies}"
askpass=$(mktemp)
trap 'rm -f "$askpass"' EXIT
cat > "$askpass" <<'ASKPASS'
#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
case "$1" in
    *git.in.encos.cn*) ;;
    *) exit 1 ;;
esac
case "$1" in
    *Username*) printf '%s\n' "${GIT_PRIVATE_USER:-bismarckkk}" ;;
    *Password*) printf '%s\n' "$USER_PASSWORD" ;;
    *) exit 1 ;;
esac
ASKPASS
chmod 700 "$askpass"
export GIT_ASKPASS="$askpass" GIT_TERMINAL_PROMPT=0
if [[ ${1:-submodules} == igh ]]; then
    git -c credential.helper= clone --depth 1 https://git.in.encos.cn/EncosRobotics-Stack/ethercat.git ethercat
else
    git submodule sync --recursive
    git -c credential.helper= submodule update --init --recursive
fi
