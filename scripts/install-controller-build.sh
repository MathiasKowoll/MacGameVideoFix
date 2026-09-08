#!/usr/bin/env bash
#
# Take what build-controller-bus.sh produced and put it where the repository
# expects it.
#
#     scripts/install-controller-build.sh [build dir] [--check]
#
# WHY THIS EXISTS. The same reason install-engine-build.sh does for the media
# pair: the last step of a build lived in somebody's habit, and a habit copies
# the wrong thing without noticing. Here the wrong things are two. An unstripped
# file -- the build tree keeps one beside the shipped one, four times the size,
# and a plain cp of the wrong name would ship a symbol table and six debug
# sections into every engine. And a stamp that disagrees with the media stamps
# about which engine is supported: both sets must record 26.3.0.39832, because
# the README says that build is the supported engine and the only one, and a
# second set quietly naming another would make that sentence false.
#
# So both are checked here, before anything is copied, and the copy goes to both
# places the set lives: the flat engine-controller-* files beside the installer,
# and the laid-out mirror in runtime/engine-payload-controller/ that a patcher
# overlays. check-builds.sh compares the two, so refreshing one without the
# other leaves the tree reporting drift that nobody introduced on purpose.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
RUNTIME="$ROOT/runtime"
MIRROR="$RUNTIME/engine-payload-controller"

BUILD="${MGVF_BUILD_OUT:-$HOME/Development/mgvf-winegstreamer-build}"
MINGW_BIN="${MINGW_BIN:-$HOME/.local/cxge/toolchains/llvm-mingw/bin}"
CHECK=0
for a in "$@"; do
  case "$a" in
    --check) CHECK=1 ;;
    -*)      sed -n '3,6p' "$0" >&2; exit 1 ;;
    *)       BUILD="$a" ;;
  esac
done

say() { printf '  %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

json_field() { /usr/bin/sed -n "s/.*\"$2\": *\"\([^\"]*\)\".*/\1/p" "$1"; }

READOBJ="$MINGW_BIN/llvm-readobj"; [ -x "$READOBJ" ] || READOBJ="$(command -v llvm-readobj 2>/dev/null || true)"
OBJDUMP="$MINGW_BIN/llvm-objdump"; [ -x "$OBJDUMP" ] || OBJDUMP="$(command -v llvm-objdump 2>/dev/null || true)"
[ -n "$READOBJ" ] && [ -n "$OBJDUMP" ] || die "no llvm-readobj / llvm-objdump -- set MINGW_BIN, or install llvm-mingw"

echo "[1/4] what the build produced"
for f in controller-built-for.json winebus.sys setupapi.dll ntoskrnl.exe; do
  [ -f "$BUILD/$f" ] || die "no $f in $BUILD -- run scripts/build-controller-bus.sh first"
done
app="$(json_field "$BUILD/controller-built-for.json" engine_app)"
ver="$(json_field "$BUILD/controller-built-for.json" engine_version)"
pat="$(json_field "$BUILD/controller-built-for.json" patches)"
[ -n "$app" ] || die "controller-built-for.json names no engine_app; refusing to guess where this goes"
say "built for : $app ($ver)"
say "patches   : $pat"

# Stripped, or it does not go in. build-controller-bus.sh strips and proves it;
# this is the check that a hand copy of the wrong file cannot get past.
for f in winebus.sys setupapi.dll ntoskrnl.exe; do
  if "$OBJDUMP" -h "$BUILD/$f" | /usr/bin/grep -q '\.debug_'; then
    die "$f in $BUILD carries .debug_ sections; it is the unstripped file. Rebuild with scripts/build-controller-bus.sh"
  fi
  syms="$("$READOBJ" --file-header "$BUILD/$f" | /usr/bin/sed -n 's/.*SymbolCount: *//p')"
  [ "$syms" = 0 ] || die "$f in $BUILD has a symbol table ($syms symbols); it is not the stripped file"
  exports="$("$READOBJ" --coff-exports "$BUILD/$f" | /usr/bin/grep -cE '^[[:space:]]*Name:' || true)"
  imports="$("$READOBJ" --coff-imports "$BUILD/$f" | /usr/bin/grep -cE '^[[:space:]]*Symbol:' || true)"
  say "$(printf '%-12s %8s bytes, %4s exports, %4s imported symbols, stripped' "$f" "$(stat -f %z "$BUILD/$f")" "$exports" "$imports")"
done

echo "[2/4] the engine it names against the media sets"
# Every media stamp records the one supported engine version. The optional set
# must say the same, or the two sets disagree about what this project supports.
for j in "$RUNTIME"/engine-built-for*.json; do
  [ -f "$j" ] || continue
  mv_="$(json_field "$j" engine_version)"
  [ "$mv_" = "$ver" ] || die "controller-built-for.json says engine $ver; $(basename "$j") says $mv_.
       The two sets must agree about the supported engine. Rebuild against it."
done
say "engine    : $ver, the same as every engine-built-for*.json"
say "flat      : runtime/engine-controller-{winebus.sys,setupapi.dll,ntoskrnl.exe,built-for.json}"
say "mirror    : runtime/engine-payload-controller/wine/x86_64-windows/ and built-for.json"

echo "[3/4] copying"
if [ "$CHECK" = 1 ]; then
  say "--check: nothing was copied."
  exit 0
fi
cp "$BUILD/winebus.sys"  "$RUNTIME/engine-controller-winebus.sys"
cp "$BUILD/setupapi.dll" "$RUNTIME/engine-controller-setupapi.dll"
cp "$BUILD/ntoskrnl.exe" "$RUNTIME/engine-controller-ntoskrnl.exe"
cp "$BUILD/controller-built-for.json" "$RUNTIME/engine-controller-built-for.json"
say "runtime/engine-controller-* refreshed"
mkdir -p "$MIRROR/wine/x86_64-windows"
cp "$BUILD/winebus.sys"  "$MIRROR/wine/x86_64-windows/winebus.sys"
cp "$BUILD/setupapi.dll" "$MIRROR/wine/x86_64-windows/setupapi.dll"
cp "$BUILD/ntoskrnl.exe" "$MIRROR/wine/x86_64-windows/ntoskrnl.exe"
cp "$BUILD/controller-built-for.json" "$MIRROR/built-for.json"
say "runtime/engine-payload-controller/ refreshed to match"
say "if the sizes or counts above moved, refresh the table in runtime/engine-payload-controller/README.md:"
say "check-builds.sh reads the export and import counts out of it."

echo "[4/4] the tree still agrees with itself"
# Run after, not before: this is the step that can break it.
if "$RUNTIME/check-builds.sh" >/tmp/mgvf-install-controller.log 2>&1; then
  say "check-builds.sh is clean"
else
  /usr/bin/grep -iE "drift|stale|missing|payload|controller" /tmp/mgvf-install-controller.log | sed 's/^/  /' >&2 || true
  die "check-builds.sh is not clean -- see /tmp/mgvf-install-controller.log"
fi
