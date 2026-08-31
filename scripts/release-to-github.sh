#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: release-to-github.sh [options]

Required options:
  --source-repo URL              Gitea source repository URL
  --github-repo OWNER/REPO       GitHub repository used by the REST API
  --github-repo-url URL          GitHub repository Git URL
  --gitea-url URL                Gitea base URL
  --gitea-package-owner OWNER    Owner of the Gitea Debian package registry

Optional options:
  --source-ref REF               Source branch or tag (default: main)
  --github-branch BRANCH         GitHub snapshot branch (default: main)
  --debian-distributions CSV     Debian distributions (default: jammy,noble)
  --debian-architectures CSV     Debian architectures (default: amd64,arm64)
  --debian-component NAME        Debian component (default: main)
  --git-user-name NAME           Snapshot commit author name
  --git-user-email EMAIL         Snapshot commit author email
  --github-api-url URL           GitHub REST API base URL
  --github-upload-url URL        GitHub uploads base URL
  --release-version VERSION      Use a version supplied by the main branch
  --version-output-file PATH     Write the selected version to a file
  --snapshot-only                Push the branch snapshot without a Release
  --help                         Show this help

Required environment variables:
  GITEA_USERNAME                 Gitea user name
  GITEA_TOKEN                    Gitea access token or password
  GH_TOKEN                       GitHub token with repository/release access
EOF
}

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

require_value() {
  local option=$1
  local value=${2:-}
  [[ -n "${value}" ]] || die "${option} requires a value"
}

source_repo=""
source_ref="main"
github_repo=""
github_repo_url=""
github_branch="main"
gitea_url=""
gitea_package_owner=""
debian_distributions="jammy,noble"
debian_architectures="amd64,arm64"
debian_component="main"
git_user_name="Encos Release Bot"
git_user_email="release-bot@encos.cn"
github_api_url="https://api.github.com"
github_upload_url="https://uploads.github.com"
release_version=""
version_output_file=""
snapshot_only=false

while (($#)); do
  case "$1" in
    --source-repo)
      require_value "$1" "${2:-}"
      source_repo=$2
      shift 2
      ;;
    --source-ref)
      require_value "$1" "${2:-}"
      source_ref=$2
      shift 2
      ;;
    --github-repo)
      require_value "$1" "${2:-}"
      github_repo=$2
      shift 2
      ;;
    --github-repo-url)
      require_value "$1" "${2:-}"
      github_repo_url=$2
      shift 2
      ;;
    --github-branch)
      require_value "$1" "${2:-}"
      github_branch=$2
      shift 2
      ;;
    --gitea-url)
      require_value "$1" "${2:-}"
      gitea_url=$2
      shift 2
      ;;
    --gitea-package-owner)
      require_value "$1" "${2:-}"
      gitea_package_owner=$2
      shift 2
      ;;
    --debian-distributions)
      require_value "$1" "${2:-}"
      debian_distributions=$2
      shift 2
      ;;
    --debian-architectures)
      require_value "$1" "${2:-}"
      debian_architectures=$2
      shift 2
      ;;
    --debian-component)
      require_value "$1" "${2:-}"
      debian_component=$2
      shift 2
      ;;
    --git-user-name)
      require_value "$1" "${2:-}"
      git_user_name=$2
      shift 2
      ;;
    --git-user-email)
      require_value "$1" "${2:-}"
      git_user_email=$2
      shift 2
      ;;
    --github-api-url)
      require_value "$1" "${2:-}"
      github_api_url=$2
      shift 2
      ;;
    --github-upload-url)
      require_value "$1" "${2:-}"
      github_upload_url=$2
      shift 2
      ;;
    --release-version)
      require_value "$1" "${2:-}"
      release_version=$2
      shift 2
      ;;
    --version-output-file)
      require_value "$1" "${2:-}"
      version_output_file=$2
      shift 2
      ;;
    --snapshot-only)
      snapshot_only=true
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

[[ -n "${source_repo}" ]] || die "--source-repo is required"
[[ -n "${github_repo}" ]] || die "--github-repo is required"
[[ "${github_repo}" == */* ]] || die "--github-repo must use OWNER/REPO format"
[[ -n "${github_repo_url}" ]] || die "--github-repo-url is required"
[[ -n "${gitea_url}" ]] || die "--gitea-url is required"
[[ -n "${gitea_package_owner}" ]] || die "--gitea-package-owner is required"
[[ -n "${GITEA_USERNAME:-}" ]] || die "GITEA_USERNAME is required"
[[ -n "${GITEA_TOKEN:-}" ]] || die "GITEA_TOKEN is required"
[[ -n "${GH_TOKEN:-}" ]] || die "GH_TOKEN is required"

for command_name in git curl python3 rsync; do
  command -v "${command_name}" >/dev/null 2>&1 || die "required command not found: ${command_name}"
done

make_basic_auth() {
  RELEASE_AUTH_USER=$1 RELEASE_AUTH_TOKEN=$2 python3 - <<'PY'
import base64
import os

credential = f"{os.environ['RELEASE_AUTH_USER']}:{os.environ['RELEASE_AUTH_TOKEN']}"
print(base64.b64encode(credential.encode()).decode())
PY
}

url_origin() {
  python3 - "$1" <<'PY'
import sys
from urllib.parse import urlparse

parsed = urlparse(sys.argv[1])
if parsed.scheme in {"http", "https"} and parsed.hostname:
    print(f"{parsed.scheme}://{parsed.netloc}/")
PY
}

git_with_auth() {
  local auth_scope=$1
  local username=$2
  local token=$3
  shift 3

  local encoded
  encoded=$(make_basic_auth "${username}" "${token}")
  local auth_key="http.${auth_scope%/}/.extraHeader"
  GIT_CONFIG_COUNT=1 \
    GIT_CONFIG_KEY_0="${auth_key}" \
    GIT_CONFIG_VALUE_0="Authorization: Basic ${encoded}" \
    git "$@"
}

python3 - "${source_repo}" "${gitea_url}" "${github_repo}" "${github_repo_url}" \
  "${github_api_url}" "${github_upload_url}" <<'PY'
import sys
from pathlib import Path
from urllib.parse import urlparse

(
    source_repo,
    gitea_url,
    github_repo,
    github_repo_url,
    github_api_url,
    github_upload_url,
) = sys.argv[1:]


def validate_https(url, label):
    parsed = urlparse(url)
    if parsed.scheme != "https" or not parsed.hostname:
        raise SystemExit(f"{label} must use HTTPS")
    if parsed.username or parsed.password:
        raise SystemExit(f"{label} must not contain credentials")
    return parsed


gitea = validate_https(gitea_url, "--gitea-url")
source = urlparse(source_repo)
if source.scheme in {"http", "https"}:
    source = validate_https(source_repo, "--source-repo")
    if (source.hostname, source.port) != (gitea.hostname, gitea.port):
        raise SystemExit("--source-repo must use the configured Gitea host")
elif not Path(source_repo).exists():
    raise SystemExit("--source-repo must be an HTTPS URL or an existing local path")

target = urlparse(github_repo_url)
if target.scheme in {"http", "https"}:
    target = validate_https(github_repo_url, "--github-repo-url")
    api = validate_https(github_api_url, "--github-api-url")
    upload = validate_https(github_upload_url, "--github-upload-url")
    expected_git_host = "github.com" if api.hostname == "api.github.com" else api.hostname
    if target.hostname != expected_git_host:
        raise SystemExit("Git remote host does not match the GitHub API host")
    if api.hostname == "api.github.com" and upload.hostname != "uploads.github.com":
        raise SystemExit("GitHub upload host does not match the public GitHub API")
    if api.hostname != "api.github.com" and upload.hostname != api.hostname:
        raise SystemExit("GitHub upload host does not match the GitHub API host")
    target_path = target.path.strip("/")
    if target_path.endswith(".git"):
        target_path = target_path[:-4]
    if target_path.casefold() != github_repo.casefold():
        raise SystemExit("--github-repo-url does not match --github-repo")
elif not (Path(github_repo_url).exists() or github_repo_url.startswith("/")):
    raise SystemExit("--github-repo-url must be an HTTPS URL or a local path")
PY

work_dir=$(mktemp -d)
cleanup() {
  if [[ -n "${work_dir:-}" && -d "${work_dir}" && "${work_dir}" == /tmp/* ]]; then
    rm -rf "${work_dir}"
  fi
}
trap cleanup EXIT

source_dir="${work_dir}/source"
target_dir="${work_dir}/target"
package_dir="${work_dir}/packages"
mkdir -p "${package_dir}"

printf 'Cloning source snapshot from %s (%s)\n' "${source_repo}" "${source_ref}"
git_with_auth "${gitea_url}" "${GITEA_USERNAME}" "${GITEA_TOKEN}" \
  -c protocol.file.allow=always clone --quiet --depth 1 --branch "${source_ref}" \
  "${source_repo}" "${source_dir}"

update_submodules_safely() {
  local repository_dir=$1
  [[ -f "${repository_dir}/.gitmodules" ]] || return 0

  local allow_local=false
  if [[ "${source_repo}" != http://* && "${source_repo}" != https://* ]]; then
    allow_local=true
  fi

  local paths_file="${work_dir}/submodule-paths-$RANDOM"
  SOURCE_REMOTE=$(git -C "${repository_dir}" remote get-url origin) \
    GITEA_BASE_URL="${gitea_url}" \
    ALLOW_LOCAL_SUBMODULES="${allow_local}" \
    python3 - "${repository_dir}/.gitmodules" >"${paths_file}" <<'PY'
import configparser
import os
import sys
from urllib.parse import urljoin, urlparse

config = configparser.ConfigParser(interpolation=None)
config.read(sys.argv[1], encoding="utf-8")
trusted = urlparse(os.environ["GITEA_BASE_URL"])
allow_local = os.environ["ALLOW_LOCAL_SUBMODULES"] == "true"
source_remote = os.environ["SOURCE_REMOTE"]
changed = False

for section in config.sections():
    path = config.get(section, "path", fallback="")
    url = config.get(section, "url", fallback="")
    if not path or not url or "\n" in path:
        raise SystemExit(f"Invalid submodule entry: {section}")
    parsed_url = urlparse(url)
    is_local_url = parsed_url.scheme in {"", "file"}
    if not (allow_local and is_local_url):
        resolved = urlparse(urljoin(source_remote, url))
        if resolved.username or resolved.password:
            raise SystemExit(f"Submodule {path!r} URL must not contain credentials")
        if resolved.scheme == "http":
            if resolved.hostname != trusted.hostname or resolved.port is not None:
                raise SystemExit(f"Submodule {path!r} uses an untrusted HTTP URL")
            resolved = resolved._replace(scheme="https")
            config.set(section, "url", resolved.geturl())
            changed = True
            print(
                f"Upgraded submodule {path!r} URL from HTTP to HTTPS",
                file=sys.stderr,
            )
        if resolved.scheme != "https" or not resolved.hostname:
            raise SystemExit(f"Submodule {path!r} must use HTTPS")
        if (resolved.hostname, resolved.port) != (trusted.hostname, trusted.port):
            raise SystemExit(f"Submodule {path!r} is outside the configured Gitea host")
    print(path)

if changed:
    with open(sys.argv[1], "w", encoding="utf-8") as output:
        config.write(output)
PY

  local submodule_paths=()
  mapfile -t submodule_paths <"${paths_file}"
  ((${#submodule_paths[@]})) || return 0
  git_with_auth "${gitea_url}" "${GITEA_USERNAME}" "${GITEA_TOKEN}" \
    -c protocol.file.allow=always -C "${repository_dir}" submodule update \
    --init --depth 1 -- "${submodule_paths[@]}"

  local submodule_path
  for submodule_path in "${submodule_paths[@]}"; do
    update_submodules_safely "${repository_dir}/${submodule_path}"
  done
}

update_submodules_safely "${source_dir}"

if [[ -n "${release_version}" ]]; then
  [[ "${release_version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || \
    die "--release-version must use X.Y.Z format"
  version=${release_version}
else
  version=$(python3 - "${source_dir}/CMakeLists.txt" <<'PY'
import re
import sys
from pathlib import Path

content = Path(sys.argv[1]).read_text(encoding="utf-8")
match = re.search(
    r"project\s*\(\s*encos_driver\s+VERSION\s+([0-9]+(?:\.[0-9]+){2})\b",
    content,
    re.IGNORECASE,
)
if not match:
    raise SystemExit("Unable to extract encos_driver version from CMakeLists.txt")
print(match.group(1))
PY
  )
fi
if [[ -n "${version_output_file}" ]]; then
  printf '%s\n' "${version}" >"${version_output_file}"
fi
tag="v${version}"

git init --quiet "${target_dir}"
git -C "${target_dir}" remote add origin "${github_repo_url}"
github_auth_scope=$(url_origin "${github_repo_url}")
if [[ -z "${github_auth_scope}" ]]; then
  github_auth_scope="https://github.com/"
fi
previous_remote_commit=""

set +e
git_with_auth "${github_auth_scope}" "x-access-token" "${GH_TOKEN}" -C "${target_dir}" ls-remote \
  --exit-code --heads origin "refs/heads/${github_branch}" >/dev/null 2>&1
remote_branch_status=$?
set -e

case "${remote_branch_status}" in
  0)
    git_with_auth "${github_auth_scope}" "x-access-token" "${GH_TOKEN}" -C "${target_dir}" fetch --quiet \
      --depth 1 origin "refs/heads/${github_branch}"
    git -C "${target_dir}" checkout --quiet -B "${github_branch}" FETCH_HEAD
    previous_remote_commit=$(git -C "${target_dir}" rev-parse FETCH_HEAD)
    ;;
  2)
    git -C "${target_dir}" symbolic-ref HEAD "refs/heads/${github_branch}"
    ;;
  *)
    die "unable to inspect GitHub branch ${github_branch}"
    ;;
esac

rsync -a --delete --delete-excluded \
  --filter='protect /.git/' \
  --exclude='.git' \
  --exclude='.gitmodules' \
  --exclude='/.gitea/' \
  --exclude='/openspec/' \
  "${source_dir}/" "${target_dir}/"

git -C "${target_dir}" add --all
if git -C "${target_dir}" diff --cached --quiet; then
  printf 'GitHub snapshot is unchanged; nothing to publish.\n'
  exit 0
fi

if [[ "${snapshot_only}" == true ]]; then
  git -C "${target_dir}" config user.name "${git_user_name}"
  git -C "${target_dir}" config user.email "${git_user_email}"
  git -C "${target_dir}" commit --quiet -m "release: 发布${tag}"
  git_with_auth "${github_auth_scope}" "x-access-token" "${GH_TOKEN}" \
    -C "${target_dir}" push --quiet origin "HEAD:refs/heads/${github_branch}"
  printf 'Published source snapshot %s to GitHub branch %s.\n' \
    "${tag}" "${github_branch}"
  exit 0
fi

set +e
git_with_auth "${github_auth_scope}" "x-access-token" "${GH_TOKEN}" \
  -C "${target_dir}" ls-remote --exit-code --tags origin "refs/tags/${tag}" \
  >/dev/null 2>&1
tag_status=$?
set -e
case "${tag_status}" in
  0)
    die "GitHub tag ${tag} already exists"
    ;;
  2)
    ;;
  *)
    die "unable to inspect GitHub tag ${tag}"
    ;;
esac

gitea_auth=$(make_basic_auth "${GITEA_USERNAME}" "${GITEA_TOKEN}")
IFS=',' read -r -a distributions <<<"${debian_distributions}"
IFS=',' read -r -a architectures <<<"${debian_architectures}"
package_manifest="${work_dir}/package-manifest.tsv"
: >"${package_manifest}"

for distribution in "${distributions[@]}"; do
  distribution=${distribution//[[:space:]]/}
  [[ -n "${distribution}" ]] || die "empty Debian distribution"
  for architecture in "${architectures[@]}"; do
    architecture=${architecture//[[:space:]]/}
    [[ -n "${architecture}" ]] || die "empty Debian architecture"
    index_file="${work_dir}/Packages-${distribution}-${architecture}"
    index_url="${gitea_url%/}/api/packages/${gitea_package_owner}/debian/dists/${distribution}/${debian_component}/binary-${architecture}/Packages"
    curl --fail-with-body --silent --show-error --location \
      --header "Authorization: Basic ${gitea_auth}" \
      --output "${index_file}" "${index_url}"
    python3 - "${index_file}" "${version}" "${distribution}" "${architecture}" \
      >>"${package_manifest}" <<'PY'
import re
import sys
from pathlib import Path

index_path, wanted_version, distribution, architecture = sys.argv[1:]
matches = 0
for paragraph in Path(index_path).read_text(encoding="utf-8").split("\n\n"):
    fields = {}
    for line in paragraph.splitlines():
        if ": " in line:
            key, value = line.split(": ", 1)
            fields[key] = value
    if fields.get("Version") != wanted_version:
        continue
    required = ("Filename", "Architecture", "Size", "SHA256")
    if any(not fields.get(field) for field in required):
        raise SystemExit(f"Incomplete Debian index entry in {index_path}")
    if fields["Architecture"] != architecture:
        raise SystemExit(f"Unexpected architecture in {index_path}: {fields['Architecture']}")
    if not fields["Filename"].endswith(".deb"):
        raise SystemExit(f"Unexpected package filename in {index_path}")
    if not fields["Size"].isdigit() or not re.fullmatch(r"[0-9a-fA-F]{64}", fields["SHA256"]):
        raise SystemExit(f"Invalid package integrity metadata in {index_path}")
    print(
        f"{distribution}\t{architecture}\t{fields['Filename']}\t"
        f"{fields['Size']}\t{fields['SHA256'].lower()}"
    )
    matches += 1
if not matches:
    raise SystemExit(
        f"No Debian package version {wanted_version} for {distribution}/{architecture}"
    )
PY
  done
done

sort -u -o "${package_manifest}" "${package_manifest}"
[[ -s "${package_manifest}" ]] || die "no Debian packages found for version ${version}"

declare -A downloaded_names=()
while IFS=$'\t' read -r distribution architecture package_path package_size package_sha256; do
  package_name=$(basename "${package_path}")
  asset_name="${package_name%.deb}_${distribution}.deb"
  existing_path=${downloaded_names["${asset_name}"]:-}
  if [[ -n "${existing_path}" && "${existing_path}" != "${package_path}" ]]; then
    die "multiple Debian packages use the asset name ${asset_name}"
  fi
  if [[ -n "${existing_path}" ]]; then
    continue
  fi
  downloaded_names["${asset_name}"]=${package_path}
  package_url="${gitea_url%/}/api/packages/${gitea_package_owner}/debian/${package_path#/}"
  curl --fail-with-body --silent --show-error --location \
    --header "Authorization: Basic ${gitea_auth}" \
    --output "${package_dir}/${asset_name}" "${package_url}"
  python3 - "${package_dir}/${asset_name}" "${package_size}" "${package_sha256}" <<'PY'
import hashlib
import sys
from pathlib import Path

package_path = Path(sys.argv[1])
expected_size = int(sys.argv[2])
expected_sha256 = sys.argv[3]
content = package_path.read_bytes()
if len(content) != expected_size:
    raise SystemExit(f"Size mismatch for {package_path.name}")
if hashlib.sha256(content).hexdigest() != expected_sha256:
    raise SystemExit(f"SHA256 mismatch for {package_path.name}")
PY
done <"${package_manifest}"

git -C "${target_dir}" config user.name "${git_user_name}"
git -C "${target_dir}" config user.email "${git_user_email}"
git -C "${target_dir}" commit --quiet -m "release: 发布${tag}"
new_commit=$(git -C "${target_dir}" rev-parse HEAD)
publication_started=true
draft_release_id=""
rollback_publication() {
  local status=$?
  if [[ "${publication_started:-false}" != true || "${status}" -eq 0 ]]; then
    return "${status}"
  fi

  set +e
  printf 'Publishing failed; rolling back the GitHub branch and draft release.\n' >&2
  local cleanup_failed=false
  if [[ -n "${draft_release_id:-}" ]]; then
    if ! curl --fail-with-body --silent --show-error --request DELETE \
      --header "Authorization: Bearer ${GH_TOKEN}" \
      --header "Accept: application/vnd.github+json" \
      --header "X-GitHub-Api-Version: 2022-11-28" \
      "${github_api_url%/}/repos/${github_repo}/releases/${draft_release_id}" \
      >/dev/null; then
      printf 'Rollback warning: failed to delete GitHub release %s.\n' \
        "${draft_release_id}" >&2
      cleanup_failed=true
    fi
  fi

  local tag_output=""
  if tag_output=$(git_with_auth "${github_auth_scope}" "x-access-token" "${GH_TOKEN}" \
    -C "${target_dir}" ls-remote --tags origin "refs/tags/${tag}" 2>/dev/null); then
    local remote_tag_commit=${tag_output%%$'\t'*}
    if [[ "${remote_tag_commit}" == "${new_commit}" ]]; then
      if ! git_with_auth "${github_auth_scope}" "x-access-token" "${GH_TOKEN}" \
        -C "${target_dir}" push --quiet \
        --force-with-lease="refs/tags/${tag}:${new_commit}" \
        origin ":refs/tags/${tag}"; then
        printf 'Rollback warning: failed to delete GitHub tag %s.\n' "${tag}" >&2
        cleanup_failed=true
      fi
    elif [[ -n "${remote_tag_commit}" ]]; then
      printf 'Rollback warning: GitHub tag %s points to an unexpected commit; it was not changed.\n' \
        "${tag}" >&2
      cleanup_failed=true
    fi
  else
    printf 'Rollback warning: failed to inspect GitHub tag %s.\n' "${tag}" >&2
    cleanup_failed=true
  fi

  local branch_output=""
  if branch_output=$(git_with_auth "${github_auth_scope}" "x-access-token" "${GH_TOKEN}" \
    -C "${target_dir}" ls-remote --heads origin "refs/heads/${github_branch}" 2>/dev/null); then
    local remote_branch_commit=${branch_output%%$'\t'*}
    if [[ "${remote_branch_commit}" == "${new_commit}" ]]; then
      if [[ -n "${previous_remote_commit}" ]]; then
        if ! git_with_auth "${github_auth_scope}" "x-access-token" "${GH_TOKEN}" \
          -C "${target_dir}" push --quiet \
          --force-with-lease="refs/heads/${github_branch}:${new_commit}" \
          origin "${previous_remote_commit}:refs/heads/${github_branch}"; then
          printf 'Rollback warning: failed to restore GitHub branch %s.\n' \
            "${github_branch}" >&2
          cleanup_failed=true
        fi
      elif ! git_with_auth "${github_auth_scope}" "x-access-token" "${GH_TOKEN}" \
        -C "${target_dir}" push --quiet \
        --force-with-lease="refs/heads/${github_branch}:${new_commit}" \
        origin ":refs/heads/${github_branch}"; then
        printf 'Rollback warning: failed to delete newly created GitHub branch %s.\n' \
          "${github_branch}" >&2
        cleanup_failed=true
      fi
    elif [[ "${remote_branch_commit}" != "${previous_remote_commit}" ]]; then
      printf 'Rollback warning: GitHub branch %s moved concurrently; it was not changed.\n' \
        "${github_branch}" >&2
      cleanup_failed=true
    fi
  else
    printf 'Rollback warning: failed to inspect GitHub branch %s.\n' \
      "${github_branch}" >&2
    cleanup_failed=true
  fi
  if [[ "${cleanup_failed}" == true ]]; then
    printf 'Rollback was incomplete; inspect the GitHub repository before retrying.\n' >&2
  fi
  set -e
  return "${status}"
}
trap rollback_publication ERR

git_with_auth "${github_auth_scope}" "x-access-token" "${GH_TOKEN}" \
  -C "${target_dir}" push --quiet origin "HEAD:refs/heads/${github_branch}"
printf 'Published source snapshot %s to GitHub.\n' "${tag}"

release_payload=$(python3 - "${tag}" "${github_branch}" <<'PY'
import json
import sys

tag, branch = sys.argv[1:]
print(json.dumps({
    "tag_name": tag,
    "target_commitish": branch,
    "name": tag,
    "draft": True,
    "prerelease": False,
}))
PY
)

release_response=$(curl --fail-with-body --silent --show-error \
  --request POST \
  --header "Authorization: Bearer ${GH_TOKEN}" \
  --header "Accept: application/vnd.github+json" \
  --header "X-GitHub-Api-Version: 2022-11-28" \
  --header "Content-Type: application/json" \
  --data "${release_payload}" \
  "${github_api_url%/}/repos/${github_repo}/releases")

release_id=$(RELEASE_RESPONSE="${release_response}" python3 - <<'PY'
import json
import os

response = json.loads(os.environ["RELEASE_RESPONSE"])
release_id = response.get("id")
if not release_id:
    raise SystemExit("GitHub release response did not contain an id")
print(release_id)
PY
)
draft_release_id=${release_id}

for package_file in "${package_dir}"/*.deb; do
  package_name=$(basename "${package_file}")
  encoded_name=$(python3 - "${package_name}" <<'PY'
import sys
import urllib.parse

print(urllib.parse.quote(sys.argv[1], safe=""))
PY
)
  curl --fail-with-body --silent --show-error \
    --request POST \
    --header "Authorization: Bearer ${GH_TOKEN}" \
    --header "Accept: application/vnd.github+json" \
    --header "X-GitHub-Api-Version: 2022-11-28" \
    --header "Content-Type: application/vnd.debian.binary" \
    --data-binary "@${package_file}" \
    "${github_upload_url%/}/repos/${github_repo}/releases/${release_id}/assets?name=${encoded_name}" \
    >/dev/null
done

publish_payload='{"draft":false}'
curl --fail-with-body --silent --show-error \
  --request PATCH \
  --header "Authorization: Bearer ${GH_TOKEN}" \
  --header "Accept: application/vnd.github+json" \
  --header "X-GitHub-Api-Version: 2022-11-28" \
  --header "Content-Type: application/json" \
  --data "${publish_payload}" \
  "${github_api_url%/}/repos/${github_repo}/releases/${release_id}" \
  >/dev/null

publication_started=false
trap - ERR

printf 'Created GitHub release %s with %s Debian package(s).\n' \
  "${tag}" "${#downloaded_names[@]}"
