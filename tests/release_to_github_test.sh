#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
script_path="${repo_root}/scripts/release-to-github.sh"
wrapper_path="${repo_root}/scripts/publish-github-release.sh"
test_root=$(mktemp -d)
trap 'rm -rf "${test_root}"' EXIT

export GIT_AUTHOR_NAME="Release Test"
export GIT_AUTHOR_EMAIL="release-test@example.com"
export GIT_COMMITTER_NAME="Release Test"
export GIT_COMMITTER_EMAIL="release-test@example.com"
export GIT_ALLOW_PROTOCOL=file

leaf_repo="${test_root}/leaf"
git init -q -b main "${leaf_repo}"
printf 'recursive payload\n' >"${leaf_repo}/leaf.txt"
git -C "${leaf_repo}" add .
git -C "${leaf_repo}" commit -q -m "initial leaf"

submodule_repo="${test_root}/submodule"
git init -q -b main "${submodule_repo}"
mkdir -p "${submodule_repo}/nested"
printf 'submodule payload\n' >"${submodule_repo}/nested/payload.txt"
git -C "${submodule_repo}" -c protocol.file.allow=always submodule add -q \
  "${leaf_repo}" vendor/leaf
git -C "${submodule_repo}" add .
git -C "${submodule_repo}" commit -q -m "initial submodule"

source_repo="${test_root}/source"
git init -q -b main "${source_repo}"
printf 'cmake_minimum_required(VERSION 3.18)\nproject(encos_driver VERSION 3.2.0)\n' \
  >"${source_repo}/CMakeLists.txt"
mkdir -p "${source_repo}/include" "${source_repo}/.gitea" "${source_repo}/openspec"
printf 'public header\n' >"${source_repo}/include/public.h"
printf 'private workflow\n' >"${source_repo}/.gitea/private.yml"
printf 'internal spec\n' >"${source_repo}/openspec/change.md"
git -C "${source_repo}" -c protocol.file.allow=always submodule add -q \
  "${submodule_repo}" external/example
git -C "${source_repo}" config -f .gitmodules \
  submodule.external/example.url http://gitea.test/acme/submodule.git
git -C "${source_repo}" add .
git -C "${source_repo}" commit -q -m "source snapshot"

git_config="${test_root}/gitconfig"
git config -f "${git_config}" url."${submodule_repo}".insteadOf \
  https://gitea.test/acme/submodule.git

target_seed="${test_root}/target-seed"
target_bare="${test_root}/target.git"
git init -q -b main "${target_seed}"
printf 'stale\n' >"${target_seed}/stale.txt"
mkdir -p "${target_seed}/.gitea" "${target_seed}/openspec"
printf 'stale workflow\n' >"${target_seed}/.gitea/stale.yml"
printf 'stale spec\n' >"${target_seed}/openspec/stale.md"
git -C "${target_seed}" add .
git -C "${target_seed}" commit -q -m "seed"
git clone -q --bare "${target_seed}" "${target_bare}"

fake_bin="${test_root}/bin"
mkdir -p "${fake_bin}"
curl_log="${test_root}/curl.log"
export CURL_LOG="${curl_log}"
export TARGET_BARE="${target_bare}"

cat >"${fake_bin}/curl" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

output=""
url=""
method="GET"
while (($#)); do
  case "$1" in
    --output|-o)
      output=$2
      shift 2
      ;;
    --request|-X)
      method=$2
      shift 2
      ;;
    --header|-H|--data|--data-binary)
      shift 2
      ;;
    --fail|--fail-with-body|--silent|--show-error|--location)
      shift
      ;;
    http://*|https://*)
      url=$1
      shift
      ;;
    *)
      shift
      ;;
  esac
done

printf '%s %s\n' "${method}" "${url}" >>"${CURL_LOG}"
case "${url}" in
  */dists/jammy/*/binary-amd64/Packages)
    cat >"${output}" <<'PACKAGES'
Package: libencosdriver
Version: 3.2.0
Architecture: amd64
Filename: pool/jammy/main/l/libencosdriver/libencosdriver_3.2.0_amd64.deb
Size: 9
SHA256: b65ab1ed14ad372bfc5603e921bad94b9843ecfa709b9b988dea6ce4624ee581

PACKAGES
    ;;
  */dists/jammy/*/binary-arm64/Packages)
    if [[ "${MISSING_ARM64:-}" == 1 ]]; then
      : >"${output}"
      exit 0
    fi
    cat >"${output}" <<'PACKAGES'
Package: libencosdriver
Version: 3.2.0
Architecture: arm64
Filename: pool/jammy/main/l/libencosdriver/libencosdriver_3.2.0_arm64.deb
Size: 9
SHA256: b65ab1ed14ad372bfc5603e921bad94b9843ecfa709b9b988dea6ce4624ee581

PACKAGES
    ;;
  */dists/noble/*/binary-amd64/Packages)
    cat >"${output}" <<'PACKAGES'
Package: libencosdriver
Version: 3.2.0
Architecture: amd64
Filename: pool/noble/main/l/libencosdriver/libencosdriver_3.2.0_amd64.deb
Size: 9
SHA256: b65ab1ed14ad372bfc5603e921bad94b9843ecfa709b9b988dea6ce4624ee581

PACKAGES
    ;;
  */dists/noble/*/binary-arm64/Packages)
    if [[ "${MISSING_ARM64:-}" == 1 ]]; then
      : >"${output}"
      exit 0
    fi
    cat >"${output}" <<'PACKAGES'
Package: libencosdriver
Version: 3.2.0
Architecture: arm64
Filename: pool/noble/main/l/libencosdriver/libencosdriver_3.2.0_arm64.deb
Size: 9
SHA256: b65ab1ed14ad372bfc5603e921bad94b9843ecfa709b9b988dea6ce4624ee581

PACKAGES
    ;;
  */repos/acme/mirror/releases)
    printf '{"id":42,"upload_url":"https://uploads.github.test/repos/acme/mirror/releases/42/assets{?name,label}"}\n'
    ;;
  */repos/acme/mirror/releases/42)
    if [[ "${method}" == PATCH && "${FAIL_PATCH:-}" == 1 ]]; then
      git --git-dir="${TARGET_BARE}" update-ref refs/tags/v3.2.0 main
      exit 22
    fi
    printf '{"id":42,"draft":false}\n'
    ;;
  *uploads.github.test*/assets*)
    if [[ "${FAIL_UPLOAD:-}" == 1 ]]; then
      exit 22
    fi
    printf '{"state":"uploaded"}\n'
    ;;
  *.deb)
    printf 'fake deb\n' >"${output}"
    ;;
  *)
    printf 'unexpected URL: %s\n' "${url}" >&2
    exit 1
    ;;
esac
EOF
chmod +x "${fake_bin}/curl"

run_release() {
  PATH="${fake_bin}:${PATH}" \
    GIT_CONFIG_GLOBAL="${git_config}" \
    GITEA_USERNAME="gitea-user" \
    GITEA_TOKEN="gitea-token" \
    GH_TOKEN="github-token" \
    bash "${script_path}" \
      --source-repo "${source_repo}" \
      --source-ref main \
      --github-repo acme/mirror \
      --github-repo-url "${target_bare}" \
      --github-branch main \
      --github-api-url https://api.github.test \
      --github-upload-url https://uploads.github.test \
      --gitea-url https://gitea.test \
      --gitea-package-owner packages \
      --debian-distributions jammy,noble \
      --debian-architectures amd64,arm64
}

run_release

mirror_checkout="${test_root}/mirror-checkout"
git clone -q --branch main "${target_bare}" "${mirror_checkout}"
test -f "${mirror_checkout}/include/public.h"
test -f "${mirror_checkout}/external/example/nested/payload.txt"
test -f "${mirror_checkout}/external/example/vendor/leaf/leaf.txt"
test ! -e "${mirror_checkout}/.gitmodules"
test ! -e "${mirror_checkout}/.gitea"
test ! -e "${mirror_checkout}/openspec"
test ! -e "${mirror_checkout}/stale.txt"
test ! -e "${mirror_checkout}/external/example/.git"
test ! -e "${mirror_checkout}/external/example/vendor/leaf/.git"
test "$(git -C "${mirror_checkout}" log -1 --format=%s)" = "release: 发布v3.2.0"
test "$(grep -c 'uploads.github.test.*/assets' "${curl_log}")" -eq 4
grep -q 'name=libencosdriver_3.2.0_amd64_jammy.deb' "${curl_log}"
grep -q 'name=libencosdriver_3.2.0_amd64_noble.deb' "${curl_log}"
grep -q 'PATCH https://api.github.test/repos/acme/mirror/releases/42' "${curl_log}"

commit_count_before=$(git --git-dir="${target_bare}" rev-list --count main)
: >"${curl_log}"
run_release
commit_count_after=$(git --git-dir="${target_bare}" rev-list --count main)
test "${commit_count_before}" = "${commit_count_after}"
test ! -s "${curl_log}"

git -C "${source_repo}" switch -q -c develop
printf 'develop snapshot\n' >"${source_repo}/develop.txt"
printf 'cmake_minimum_required(VERSION 3.18)\nproject(encos_driver)\n' \
  >"${source_repo}/CMakeLists.txt"
git -C "${source_repo}" add .
git -C "${source_repo}" commit -q -m "develop snapshot"
git -C "${source_repo}" switch -q main

multi_branch_env="${test_root}/multi-branch.env"
cat >"${multi_branch_env}" <<EOF
GITEA_USERNAME=gitea-user
GITEA_TOKEN=gitea-token
GH_TOKEN=github-token
SOURCE_REPO=${source_repo}
MAIN_BRANCH=main
OPTIONAL_BRANCHES=develop
GITHUB_RELEASE_REPOSITORY=acme/mirror
GITHUB_RELEASE_REPOSITORY_URL=${target_bare}
GITEA_URL=https://gitea.test
GITEA_PACKAGE_OWNER=packages
DEBIAN_DISTRIBUTIONS=jammy,noble
DEBIAN_ARCHITECTURES=amd64,arm64
GITHUB_API_URL=https://api.github.test
GITHUB_UPLOAD_URL=https://uploads.github.test
EOF

: >"${curl_log}"
PATH="${fake_bin}:${PATH}" GIT_CONFIG_GLOBAL="${git_config}" \
  bash "${wrapper_path}" --env-file "${multi_branch_env}"
test ! -s "${curl_log}"
develop_checkout="${test_root}/develop-checkout"
git clone -q --branch develop "${target_bare}" "${develop_checkout}"
test -f "${develop_checkout}/develop.txt"
test "$(git -C "${develop_checkout}" log -1 --format=%s)" = "release: 发布v3.2.0"

printf 'changed snapshot\n' >"${source_repo}/include/public.h"
git -C "${source_repo}" add .
git -C "${source_repo}" commit -q -m "changed source snapshot"

: >"${curl_log}"
set +e
MISSING_ARM64=1 run_release
missing_status=$?
set -e
test "${missing_status}" -ne 0
test "$(git --git-dir="${target_bare}" rev-list --count main)" = "${commit_count_after}"

: >"${curl_log}"
set +e
FAIL_UPLOAD=1 run_release
upload_status=$?
set -e
test "${upload_status}" -ne 0
test "$(git --git-dir="${target_bare}" rev-list --count main)" = "${commit_count_after}"
grep -q 'DELETE https://api.github.test/repos/acme/mirror/releases/42' "${curl_log}"

: >"${curl_log}"
set +e
FAIL_PATCH=1 run_release
patch_status=$?
set -e
test "${patch_status}" -ne 0
test "$(git --git-dir="${target_bare}" rev-list --count main)" = "${commit_count_after}"
test -z "$(git --git-dir="${target_bare}" tag --list v3.2.0)"
grep -q 'DELETE https://api.github.test/repos/acme/mirror/releases/42' "${curl_log}"

unsafe_urls=(
  'http://example.test/acme/unsafe.git'
  'https://token@gitea.test/acme/unsafe.git'
  'https://example.test/acme/unsafe.git'
)
for unsafe_url in "${unsafe_urls[@]}"; do
  git -C "${source_repo}" config -f .gitmodules \
    submodule.external/example.url "${unsafe_url}"
  git -C "${source_repo}" add .gitmodules
  git -C "${source_repo}" commit -q -m "unsafe submodule fixture"
  set +e
  run_release >/dev/null 2>&1
  unsafe_status=$?
  set -e
  test "${unsafe_status}" -ne 0
  test "$(git --git-dir="${target_bare}" rev-list --count main)" = "${commit_count_after}"
done

printf 'release_to_github_test: PASS\n'
