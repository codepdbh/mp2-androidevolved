#!/usr/bin/env bash
# Copia fija del port de DXVK a Android en work/pc/dxvk_android (no se sube al repositorio).
# Sale de la versión confirmada en git de nfsu2-recompiled, para que los cambios en curso de
# aquel proyecto no rompan esta compilación. La fuente preparada debe venir de esa receta.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NFSU2="${1:-$ROOT/../nfsu2-androidevolved/nfsu2-recompiled}"
OUT="$ROOT/work/pc/dxvk_android"
mkdir -p "$OUT/port" "$OUT/source"
git -C "$NFSU2" archive HEAD ports/dxvk_android | tar -x -C "$OUT/port" --strip-components=2
cp -r "$NFSU2/build/dxvk-source/." "$OUT/source/"
sed -i 's#"\.\./\.\./android/app/src/main/cpp/android_resolution.h"#"../../../../android/app/src/main/cpp/android_resolution.h"#' "$OUT/port/android_wsi.cpp"
sha256sum "$OUT/port/prepare.py" | cut -c1-64 > "$OUT/source/ANDROID_PREPARE_SHA256"
echo "Listo: $OUT"
