#!/usr/bin/env bash
#
# Build the proxy DLL that carries ue5-vpx-cpupath into the game.
#
# The game has no plugin hook we can use, so the patch rides in on a DLL the
# engine already loads. libogg_64.dll is a good carrier: every Unreal title
# ships it, it loads early (before any cutscene), its ABI has been frozen for
# years, and it has no D3D involvement at all -- so a proxy in front of it
# cannot disturb rendering.
#
# The proxy re-exports every symbol straight through to the renamed original
# with PE export forwarders, so at runtime the real libogg is what actually
# answers every call. We only get DllMain.
#
#   build-proxy.sh <reference libogg_64.dll> [output dir]
#
# The reference is the game's own untouched libogg_64.dll -- exports are read
# from it so the forwarder list matches exactly.
#
# Needs llvm-mingw (or any x86_64-w64-mingw32 toolchain) on PATH or in
# MINGW_BIN. Users never run this; the built DLL ships in the release.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"

usage() { sed -n '3,20p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

REF="$1"
OUT="${2:-$HERE/build}"
[ -f "$REF" ] || { echo "error: no such file: $REF" >&2; exit 1; }

MINGW_BIN="${MINGW_BIN:-$HOME/.local/cxge/toolchains/llvm-mingw/bin}"
CC=""
for candidate in "$MINGW_BIN/x86_64-w64-mingw32-clang" \
                 "$MINGW_BIN/x86_64-w64-mingw32-gcc" \
                 "$(command -v x86_64-w64-mingw32-clang 2>/dev/null || true)" \
                 "$(command -v x86_64-w64-mingw32-gcc 2>/dev/null || true)"; do
  [ -n "$candidate" ] && [ -x "$candidate" ] && { CC="$candidate"; break; }
done
[ -n "$CC" ] || {
  echo "error: no x86_64-w64-mingw32 compiler found." >&2
  echo "       set MINGW_BIN, or install llvm-mingw:" >&2
  echo "       https://github.com/mstorsjo/llvm-mingw/releases" >&2
  exit 1
}

mkdir -p "$OUT"
DEF="$OUT/libogg_64.def"
DLL="$OUT/libogg_64.dll"

# Generate the forwarder table from the reference. Each line says "our export
# named X is really libogg_real.X" -- the Windows loader resolves it on demand,
# so no thunk code runs and nothing can go wrong in the hot path.
{
  echo "LIBRARY libogg_64.dll"
  echo "EXPORTS"
  python3 "$HERE/pe.py" exports "$REF" --ordinals |
    while read -r ordinal name; do
      printf '    %s = libogg_real.%s @%s\n' "$name" "$name" "$ordinal"
    done
} > "$DEF"

count=$(($(wc -l < "$DEF") - 2))
[ "$count" -gt 0 ] || { echo "error: $REF exports nothing to forward" >&2; exit 1; }

"$CC" -shared -O2 -municode \
  -o "$DLL" "$HERE/ue5-vpx-cpupath.c" "$DEF" \
  -Wl,--enable-stdcall-fixup -lkernel32 -static-libgcc

echo "built $DLL"
echo "  $count forwarders to libogg_real.dll"
echo "  compiler: $CC"
