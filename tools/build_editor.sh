#!/bin/sh
# SPDX-License-Identifier: MIT
# The editor page's engine (app/editor/editor_wasm.c) in WebAssembly, and the page, into OUT (default
# app/build/site/edit): clang --target=wasm32 with wasm-ld, else emcc (its imports, if any, are stubbed by the page).
set -e
OUT="${1:-app/build/site/edit}"
mkdir -p "$OUT"
SRC="app/editor/editor_wasm.c src/sq80/sq80_synth.c src/sq80/sq80_patch.c src/sq80/sq80_params.c"
INC="-Isrc/sq80 -Iapp/waves -Ifm1/base/include"
if ! { command -v clang >/dev/null 2>&1 && command -v wasm-ld >/dev/null 2>&1; } && command -v emcc >/dev/null 2>&1; then
  emcc -O2 -std=gnu99 -DSQ80_FASTMATH $INC $SRC --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 \
       -sINITIAL_MEMORY=8388608 -sSTACK_SIZE=262144 -o "$OUT/sq80_editor.wasm"
else
  clang --target=wasm32 -O2 -std=gnu99 -nostdlib -ffreestanding -fvisibility=hidden -DSQ80_FASTMATH $INC $SRC \
        -Wl,--no-entry -Wl,--export-dynamic -Wl,--initial-memory=8388608 -o "$OUT/sq80_editor.wasm"
fi
cp app/editor/index.html "$OUT/index.html"
echo "editor   $OUT ($(wc -c < "$OUT/sq80_editor.wasm") B wasm)"
