#!/usr/bin/env bash
# Compila código recompilado para AArch64 con los flags de la build Android de Carbon.
# Solo comprueba que compila: no enlaza ni ejecuta nada en ARM.
#   compile_arm64_subset.sh [tag ...]     (por defecto, cuatro módulos pequeños)
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LOCAL_APP_DATA="$(cygpath -u "${LOCALAPPDATA:-$HOME/AppData/Local}")"
NDK="${ANDROID_NDK_BIN:-$LOCAL_APP_DATA/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin}"
RT="$ROOT/external/pcrecomp/runtime"
OUT="$ROOT/work/pc/arm64-test"
mkdir -p "$OUT"
[ $# -gt 0 ] || set -- t_graphmfc x_helpersmfc x_inputmfc sndmfc
ok=0; fail=0
for tag in "$@"; do
  for src in "$ROOT/work/pc/gen/$tag"/*recomp_[0-9]*.c; do
    obj="$OUT/$(basename "$src" .c).o"
    if "$NDK/clang.exe" --target=aarch64-linux-android26 -O2 -w -fwrapv -fno-strict-aliasing -ffp-contract=off \
        -DRECOMP_STRICT_ICALL -DRECOMP_LOCAL_REGS -DRECOMP_X87_WINDOW -DRECOMP_SEH_FRAMES "-DRECOMP_MEM_QUAL=" \
        -I"$RT/recomp32" -I"$ROOT/work/pc/gen/$tag" -include "$ROOT/src/runtime/pc/arm64_guest.h" \
        -c "$src" -o "$obj" 2> "$OUT/last_error.txt"; then
      ok=$((ok + 1))
    else
      fail=$((fail + 1)); echo "FALLO $src"; head -5 "$OUT/last_error.txt"
    fi
  done
done
echo "ARM64: $ok objetos correctos, $fail fallos"
[ "$fail" -eq 0 ]
