#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
script_path="${repo_root}/scripts/build-docs.sh"
test_root=$(mktemp -d)
trap 'rm -rf "${test_root}"' EXIT

fake_bin="${test_root}/bin"
build_dir="${test_root}/build-docs"
dist_dir="${test_root}/dist"
command_log="${test_root}/commands.log"
mkdir -p "${fake_bin}"

cat >"${fake_bin}/cmake" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'cmake' >>"${COMMAND_LOG}"
printf ' %q' "$@" >>"${COMMAND_LOG}"
printf '\n' >>"${COMMAND_LOG}"
EOF
cat >"${fake_bin}/make" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'make' >>"${COMMAND_LOG}"
printf ' %q' "$@" >>"${COMMAND_LOG}"
printf '\n' >>"${COMMAND_LOG}"
latex_dir=""
while (($#)); do
    if [[ $1 == -C ]]; then
        latex_dir=$2
        shift 2
    else
        shift
    fi
done
mkdir -p "${latex_dir}"
printf 'api pdf\n' >"${latex_dir}/refman.pdf"
EOF
cat >"${fake_bin}/pandoc" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'pandoc' >>"${COMMAND_LOG}"
printf ' %q' "$@" >>"${COMMAND_LOG}"
printf '\n' >>"${COMMAND_LOG}"
input=$1
output=""
shift
while (($#)); do
    if [[ $1 == -o ]]; then
        output=$2
        shift 2
    else
        shift
    fi
done
mkdir -p "$(dirname "${output}")"
printf '%s pdf\n' "$(basename "${input}" .md)" >"${output}"
EOF
cat >"${fake_bin}/rsvg-convert" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
for command_name in doxygen pdflatex makeindex xelatex; do
    cat >"${fake_bin}/${command_name}" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
done
chmod +x "${fake_bin}/cmake" "${fake_bin}/make" "${fake_bin}/pandoc" \
    "${fake_bin}/rsvg-convert" "${fake_bin}/doxygen" "${fake_bin}/pdflatex" \
    "${fake_bin}/makeindex" "${fake_bin}/xelatex"

(
    cd /tmp
    PATH="${fake_bin}:/usr/bin:/bin" \
        COMMAND_LOG="${command_log}" \
        ENCOS_DOCS_BUILD_DIR="${build_dir}" \
        ENCOS_DOCS_DIST_DIR="${dist_dir}" \
        PANDOC_CJK_MAINFONT="Test Chinese Font" \
        PANDOC_MONOFONT="Test Mono Font" \
        bash "${script_path}"
)

test "$(cat "${dist_dir}/api.pdf")" = "api pdf"
test "$(cat "${dist_dir}/arch.pdf")" = "arch pdf"
test "$(cat "${dist_dir}/changes.pdf")" = "changes pdf"
test "$(cat "${dist_dir}/using_guide.pdf")" = "using_guide pdf"
test "$(cat "${dist_dir}/logging.pdf")" = "logging pdf"

grep -Fq -- "-DENCOS_BUILD_DOCS=ON" "${command_log}"
grep -Fq -- "--target docs-latex" "${command_log}"
grep -Fq -- "make -C ${build_dir}/docs/latex pdf" "${command_log}"
grep -Fq -- "--pdf-engine=xelatex" "${command_log}"
grep -Fq -- "--resource-path=${repo_root}/docs" "${command_log}"
grep -Fq -- "--fail-if-warnings" "${command_log}"
grep -Fq -- "--highlight-style=tango" "${command_log}"
grep -Fq -- "--include-in-header=${repo_root}/docs/pandoc/code-blocks.tex" "${command_log}"
grep -Fq -- "papersize:a4" "${command_log}"
grep -Fq -- "CJKmainfont=Test\\ Chinese\\ Font" "${command_log}"
grep -Fq -- "monofont=Test\\ Mono\\ Font" "${command_log}"

printf 'build-docs script test passed\n'
