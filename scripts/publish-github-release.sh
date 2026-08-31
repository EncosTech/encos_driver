#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
env_file="${script_dir}/.env"
dry_run=false
from_environment=false

usage() {
  cat <<'EOF'
Usage: publish-github-release.sh [--env-file PATH] [--from-environment] [--dry-run]

Without arguments, load scripts/.env and perform the real publication.

Options:
  --env-file PATH  Load another dotenv-compatible shell file
  --from-environment
                   Do not load a file; use the current environment (for CI)
  --dry-run        Validate and display non-secret configuration without publishing
  --help           Show this help
EOF
}

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

while (($#)); do
  case "$1" in
    --env-file)
      [[ -n "${2:-}" ]] || die "--env-file requires a value"
      env_file=$2
      shift 2
      ;;
    --dry-run)
      dry_run=true
      shift
      ;;
    --from-environment)
      from_environment=true
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done

if [[ "${from_environment}" == false ]]; then
  [[ -f "${env_file}" ]] || die "environment file not found: ${env_file}"
  set -a
  # shellcheck source=/dev/null
  source "${env_file}"
  set +a
else
  env_file="the current environment"
fi

MAIN_BRANCH=${MAIN_BRANCH:-${SOURCE_REF:-main}}
OPTIONAL_BRANCHES=${OPTIONAL_BRANCHES:-}

required_variables=(
  GITEA_USERNAME
  GITEA_TOKEN
  GH_TOKEN
  SOURCE_REPO
  MAIN_BRANCH
  GITHUB_RELEASE_REPOSITORY
  GITHUB_RELEASE_REPOSITORY_URL
  GITEA_URL
  GITEA_PACKAGE_OWNER
  DEBIAN_DISTRIBUTIONS
  DEBIAN_ARCHITECTURES
)

for variable_name in "${required_variables[@]}"; do
  [[ -n "${!variable_name:-}" ]] || die "${variable_name} is missing in ${env_file}"
done

DEBIAN_COMPONENT=${DEBIAN_COMPONENT:-main}
GIT_USER_NAME=${GIT_USER_NAME:-Encos Release Bot}
GIT_USER_EMAIL=${GIT_USER_EMAIL:-release-bot@encos.cn}
GITHUB_API_URL=${GITHUB_API_URL:-https://api.github.com}
GITHUB_UPLOAD_URL=${GITHUB_UPLOAD_URL:-https://uploads.github.com}

git check-ref-format --branch "${MAIN_BRANCH}" >/dev/null 2>&1 || \
  die "invalid main branch name: ${MAIN_BRANCH}"

if [[ "${dry_run}" == true ]]; then
  printf 'Configuration is valid. No publication was performed.\n'
  printf '  Source: %s\n' "${SOURCE_REPO}"
  printf '  Main branch: %s\n' "${MAIN_BRANCH}"
  printf '  Optional branches: %s\n' "${OPTIONAL_BRANCHES:-<none>}"
  printf '  GitHub: %s\n' "${GITHUB_RELEASE_REPOSITORY}"
  printf '  Debian distributions: %s\n' "${DEBIAN_DISTRIBUTIONS}"
  printf '  Debian architectures: %s\n' "${DEBIAN_ARCHITECTURES}"
  exit 0
fi

run_branch() {
  local source_branch=$1
  shift
  "${script_dir}/release-to-github.sh" \
    --source-repo "${SOURCE_REPO}" \
    --source-ref "${source_branch}" \
    --github-repo "${GITHUB_RELEASE_REPOSITORY}" \
    --github-repo-url "${GITHUB_RELEASE_REPOSITORY_URL}" \
    --github-branch "${source_branch}" \
    --gitea-url "${GITEA_URL}" \
    --gitea-package-owner "${GITEA_PACKAGE_OWNER}" \
    --debian-distributions "${DEBIAN_DISTRIBUTIONS}" \
    --debian-architectures "${DEBIAN_ARCHITECTURES}" \
    --debian-component "${DEBIAN_COMPONENT}" \
    --git-user-name "${GIT_USER_NAME}" \
    --git-user-email "${GIT_USER_EMAIL}" \
    --github-api-url "${GITHUB_API_URL}" \
    --github-upload-url "${GITHUB_UPLOAD_URL}" \
    "$@"
}

version_file=$(mktemp)
cleanup() {
  if [[ -n "${version_file:-}" && -f "${version_file}" && "${version_file}" == /tmp/* ]]; then
    rm -f "${version_file}"
  fi
}
trap cleanup EXIT

printf 'Processing main branch %s\n' "${MAIN_BRANCH}"
run_branch "${MAIN_BRANCH}" --version-output-file "${version_file}"
main_version=$(<"${version_file}")
[[ "${main_version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || \
  die "main branch did not provide a valid version"

IFS=',' read -r -a optional_branches <<<"${OPTIONAL_BRANCHES}"
for optional_branch in "${optional_branches[@]}"; do
  optional_branch=${optional_branch//[[:space:]]/}
  [[ -n "${optional_branch}" ]] || continue
  [[ "${optional_branch}" != "${MAIN_BRANCH}" ]] || continue
  git check-ref-format --branch "${optional_branch}" >/dev/null 2>&1 || \
    die "invalid optional branch name: ${optional_branch}"
  printf 'Processing optional branch %s with main version %s\n' \
    "${optional_branch}" "${main_version}"
  run_branch "${optional_branch}" \
    --release-version "${main_version}" \
    --snapshot-only
done
