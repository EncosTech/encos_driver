#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${ENCOS_DOCS_BUILD_DIR:-"${repo_root}/build-docs"}
dist_dir=${ENCOS_DOCS_DIST_DIR:-"${repo_root}/docs/dist"}
pandoc_pdf_engine=${PANDOC_PDF_ENGINE:-xelatex}
pandoc_cjk_mainfont=${PANDOC_CJK_MAINFONT:-Noto Serif CJK SC}
pandoc_monofont=${PANDOC_MONOFONT:-Noto Sans Mono CJK SC}

required_commands=(cmake doxygen make pdflatex makeindex pandoc rsvg-convert
                   "${pandoc_pdf_engine}")
for required_command in "${required_commands[@]}"; do
    if ! command -v "${required_command}" >/dev/null 2>&1; then
        printf '缺少文档构建命令：%s\n' "${required_command}" >&2
        exit 1
    fi
done

mkdir -p "${dist_dir}"

cmake -S "${repo_root}" -B "${build_dir}" \
    -DENCOS_BUILD_DOCS=ON \
    -DENCOS_BUILD_TESTS=OFF
cmake --build "${build_dir}" --target docs-latex
make -C "${build_dir}/docs/latex" pdf
cp "${build_dir}/docs/latex/refman.pdf" "${dist_dir}/api.pdf"

build_markdown_pdf() {
    local input_path=$1
    local output_path=$2

    pandoc "${input_path}" \
        --standalone \
        --fail-if-warnings \
        --resource-path="${repo_root}/docs" \
        --highlight-style=tango \
        --include-in-header="${repo_root}/docs/pandoc/code-blocks.tex" \
        --pdf-engine="${pandoc_pdf_engine}" \
        -V "CJKmainfont=${pandoc_cjk_mainfont}" \
        -V "monofont=${pandoc_monofont}" \
        -V papersize:a4 \
        -V geometry:margin=2.5cm \
        -o "${output_path}"
}

build_markdown_pdf "${repo_root}/docs/changes.md" "${dist_dir}/changes.pdf"
build_markdown_pdf "${repo_root}/docs/arch.md" "${dist_dir}/arch.pdf"
build_markdown_pdf "${repo_root}/docs/using_guide.md" "${dist_dir}/using_guide.pdf"
build_markdown_pdf "${repo_root}/docs/logging.md" "${dist_dir}/logging.pdf"

printf '文档已生成到 %s\n' "${dist_dir}"
