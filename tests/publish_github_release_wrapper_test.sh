#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
wrapper="${repo_root}/scripts/publish-github-release.sh"
test_root=$(mktemp -d)
trap 'rm -rf "${test_root}"' EXIT

valid_env="${test_root}/valid.env"
cat >"${valid_env}" <<'EOF'
GITEA_USERNAME=test-user
GITEA_TOKEN=secret-gitea-token
GH_TOKEN=secret-github-token
SOURCE_REPO=https://gitea.test/acme/source.git
MAIN_BRANCH=main
OPTIONAL_BRANCHES=develop,feature-x
GITHUB_RELEASE_REPOSITORY=acme/mirror
GITHUB_RELEASE_REPOSITORY_URL=https://github.com/acme/mirror.git
GITEA_URL=https://gitea.test
GITEA_PACKAGE_OWNER=packages
DEBIAN_DISTRIBUTIONS=jammy,noble
DEBIAN_ARCHITECTURES=amd64,arm64
EOF

dry_run_output=$(bash "${wrapper}" --env-file "${valid_env}" --dry-run)
[[ "${dry_run_output}" == *"acme/mirror"* ]]
[[ "${dry_run_output}" == *"jammy,noble"* ]]
[[ "${dry_run_output}" == *"main"* ]]
[[ "${dry_run_output}" == *"develop,feature-x"* ]]
[[ "${dry_run_output}" != *"secret-gitea-token"* ]]
[[ "${dry_run_output}" != *"secret-github-token"* ]]

invalid_env="${test_root}/invalid.env"
printf 'GITEA_USERNAME=test-user\n' >"${invalid_env}"
if bash "${wrapper}" --env-file "${invalid_env}" --dry-run >/dev/null 2>&1; then
  printf 'wrapper unexpectedly accepted incomplete configuration\n' >&2
  exit 1
fi

git -C "${repo_root}" check-ignore -q scripts/.env

printf 'publish_github_release_wrapper_test: PASS\n'
