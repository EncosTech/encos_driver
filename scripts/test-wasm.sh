#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

source "$HOME/emsdk/emsdk_env.sh" >/tmp/encos-test-emsdk-env.log

export EM_CACHE="${EM_CACHE:-/tmp/encos-emscripten-cache}"
mkdir -p "$EM_CACHE"

emcmake cmake -S . -B build-wasm \
    -DENCOS_BUILD_WASM_BINDINGS=ON \
    -DENCOS_BUILD_WASM_TESTS=ON \
    -DENCOS_ENABLE_INSTALL=OFF

cmake --build build-wasm
ctest --test-dir build-wasm --output-on-failure

pnpm --dir npm typecheck
pnpm --dir npm build
pnpm --dir npm test:unit
pnpm --dir npm test:browser
