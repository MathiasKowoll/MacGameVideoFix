#!/usr/bin/env bash
#
# Build mf-probe as a proxy for whichever DLL a game already loads.
#
#   build-probe.sh <the game's carrier DLL> [output dir]
#
# The carrier has to be something the game imports directly and that has
# nothing to do with rendering or with Steam. A shipped third-party library is
# ideal -- for DYNASTY WARRIORS: ORIGINS that is libxess.dll.
#
# Produces <name>.dll (the probe) which forwards every export to
# <stem>_real.dll. Install by renaming the game's copy to <stem>_real.dll and
# dropping ours in its place.
#
# Needs llvm-mingw; set MINGW_BIN if it is not in the default place.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PE="$HERE/../runtime/pe.py"

usage() { sed -n '3,17p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

REF="$1"
# Which probe to build. mf-probe is the general one; ng4-observe is the
# DirectStorage/D3D12 probe for Ninja Gaiden 4.
SOURCE="${SOURCE:-mf-probe.c}"
OUT="${2:-$HERE/build}"
[ -f "$REF" ] || { echo "error: no such file: $REF" >&2; exit 1; }

# Once installed, the reference file on disk is the *renamed* original, so the
# carrier name cannot always be taken from it. CARRIER_NAME says what the game
# actually imports.
NAME="${CARRIER_NAME:-$(basename "$REF")}"
STEM="${NAME%.*}"
REAL="${STEM}_real"

MINGW_BIN="${MINGW_BIN:-$HOME/.local/cxge/toolchains/llvm-mingw/bin}"
CC=""
for c in "$MINGW_BIN/x86_64-w64-mingw32-clang" "$MINGW_BIN/x86_64-w64-mingw32-gcc" \
         "$(command -v x86_64-w64-mingw32-clang 2>/dev/null || true)"; do
  [ -n "$c" ] && [ -x "$c" ] && { CC="$c"; break; }
done
[ -n "$CC" ] || { echo "error: no x86_64-w64-mingw32 compiler (set MINGW_BIN)" >&2; exit 1; }

mkdir -p "$OUT"
DEF="$OUT/$STEM.def"

{
  echo "LIBRARY $NAME"
  echo "EXPORTS"
  python3 "$PE" exports "$REF" --ordinals |
    while read -r ordinal sym; do
      printf '    %s = %s.%s @%s\n' "$sym" "$REAL" "$sym" "$ordinal"
    done
} > "$DEF"

count=$(($(wc -l < "$DEF") - 2))
[ "$count" -gt 0 ] || { echo "error: $REF exports nothing to forward" >&2; exit 1; }

# Extra flags, for building the same source as something other than a probe.
# ng4-observe.c compiles either as a diagnostic -- every lever off unless asked
# for -- or, with -DNG4_FIX, as the shipped repair with them on by default.
"$CC" -shared -O2 ${CFLAGS_EXTRA:-} -o "$OUT/$NAME" "$HERE/$SOURCE" "$DEF" \
  -lmfuuid -lole32 -luuid -lkernel32 -static-libgcc

echo "built $OUT/$NAME"
echo "  $count forwarders to $REAL.dll"
echo
echo "install:"
echo "  cd \"\$(dirname \"$REF\")\""
echo "  mv $NAME $REAL.dll"
echo "  cp \"$OUT/$NAME\" ."
echo
echo "the log lands in the bottle's C:\\mf-probe.log"
