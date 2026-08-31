#!/usr/bin/env bash
#
# Take what build-winegstreamer.sh produced and put it where the repository
# expects it.
#
#     scripts/install-engine-build.sh [build dir] [--check]
#
# WHY THIS EXISTS. build-winegstreamer.sh finishes by printing where its output
# is and telling you to copy it. That last step lived in somebody's habit, and
# the habit has a trap in it: the pair is stamped with the bundle it was built
# against, install-engine-media.sh picks a set by matching that stamp, and the
# flat files carry the stamp in their FILENAME instead. Copy a build into the
# wrong suffix and you get a pair the installer refuses -- after the build, and
# for a reason that reads like a mismatch rather than a misfiling.
#
# So the suffix is not asked for and not guessed. It is read out of the
# built-for.json the build just wrote, matched against the sets already in
# runtime/, and refused if nothing matches. That is the same rule the installer
# uses, in the same direction, from the same field.
#
# It also keeps engine-payload/ in step. That folder is the same bytes as the
# unsuffixed pair laid out the way an engine wants them, and check-builds.sh
# compares the two: updating one and not the other leaves the tree reporting
# drift that nobody introduced on purpose.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
RUNTIME="$ROOT/runtime"

BUILD="${MGVF_BUILD_OUT:-$HOME/Development/mgvf-winegstreamer-build}"
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

echo "[1/4] what the build produced"
for f in built-for.json winegstreamer.so winegstreamer.dll; do
  [ -f "$BUILD/$f" ] || die "no $f in $BUILD -- run scripts/build-winegstreamer.sh first"
done
app="$(json_field "$BUILD/built-for.json" engine_app)"
ver="$(json_field "$BUILD/built-for.json" engine_version)"
pat="$(json_field "$BUILD/built-for.json" patches)"
[ -n "$app" ] || die "built-for.json names no engine_app; refusing to guess where this goes"
say "built for : $app ($ver)"
say "patches   : $pat"

echo "[2/4] which set that is"
# Matched, never guessed: the same field the installer matches on.
suffix=""; found=0
for j in "$RUNTIME"/engine-built-for*.json; do
  [ -f "$j" ] || continue
  [ "$(json_field "$j" engine_app)" = "$app" ] || continue
  s="$(basename "$j")"; s="${s#engine-built-for}"; s="${s%.json}"
  suffix="$s"; found=1; break
done
[ "$found" = 1 ] || die "no set in runtime/ is stamped for $app.
       The sets that exist are:
$(for j in "$RUNTIME"/engine-built-for*.json; do printf '         %s  ->  %s\n' "$(basename "$j")" "$(json_field "$j" engine_app)"; done)
       Adding a new engine means adding its set on purpose, not by copy."
say "set       : engine-winegstreamer${suffix}.{so,dll}"

echo "[3/4] copying"
if [ "$CHECK" = 1 ]; then
  say "--check: nothing was copied."
  say "would write runtime/engine-winegstreamer${suffix}.so, .dll and engine-built-for${suffix}.json"
  [ -z "$suffix" ] && say "would also refresh runtime/engine-payload/ (it mirrors the unsuffixed pair)"
  exit 0
fi
cp "$BUILD/winegstreamer.so"  "$RUNTIME/engine-winegstreamer${suffix}.so"
cp "$BUILD/winegstreamer.dll" "$RUNTIME/engine-winegstreamer${suffix}.dll"
cp "$BUILD/built-for.json"    "$RUNTIME/engine-built-for${suffix}.json"
say "runtime/engine-winegstreamer${suffix}.{so,dll} and its built-for.json"

# engine-payload/ mirrors the unsuffixed pair. Only that one.
if [ -z "$suffix" ]; then
  cp "$BUILD/winegstreamer.so"  "$RUNTIME/engine-payload/wine/x86_64-unix/winegstreamer.so"
  cp "$BUILD/winegstreamer.dll" "$RUNTIME/engine-payload/wine/x86_64-windows/winegstreamer.dll"
  cp "$BUILD/built-for.json"    "$RUNTIME/engine-payload/built-for.json"
  say "runtime/engine-payload/ refreshed to match"
fi

echo "[4/4] the tree still agrees with itself"
# Run after, not before: this is the step that can break it.
if "$RUNTIME/check-builds.sh" >/tmp/mgvf-install-engine.log 2>&1; then
  say "check-builds.sh is clean"
else
  /usr/bin/grep -iE "drift|stale|missing|payload" /tmp/mgvf-install-engine.log | sed 's/^/  /' >&2 || true
  die "check-builds.sh is not clean -- see /tmp/mgvf-install-engine.log"
fi
