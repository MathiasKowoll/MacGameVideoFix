#!/usr/bin/env bash
#
# Does every shipped DLL still match the source it claims to come from?
#
#     runtime/check-builds.sh            report
#     runtime/check-builds.sh --verbose  and say what differs
#
# WHY THIS EXISTS. One afternoon this repository held three different builds of
# the same proxy at once: the source said one thing, runtime/amd_ags_x64.dll was
# several revisions behind it, and the copy installed in the game was a third
# build that matched neither. Nothing anywhere said so. Every measurement taken
# against "the fix" was really a measurement of whichever of the three happened
# to be in the path, and a fix that had been rewritten twice was still shipping
# its oldest form.
#
# It asks the binaries, not a manifest. A manifest is one more thing to forget
# to update; a rebuild cannot be out of date with itself.
#
# NOTHING IS HARDCODED except one compile flag. For each shipped DLL:
#   - the carrier is read from the forwarders it exports  (X_real.<symbol>)
#   - the source is read from the log file it names        (<name>.log)
#   - the export table is read from the DLL itself, so no game has to be
#     installed for this to run
# It then rebuilds from that source and compares.
#
# Builds are not bit-reproducible -- the linker stamps a fresh build id every
# time -- so the comparison is on what the source decides: the export table,
# exactly, and the set of long printable strings, which is where the messages,
# the hook names and the format strings live.
#
# Needs llvm-mingw (or any x86_64-w64-mingw32 toolchain), same as the build
# scripts. Without one it says so and stops rather than reporting everything
# clean, because "no compiler" and "no drift" must never look the same.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PE="$HERE/pe.py"
VERBOSE=0
[ "${1:-}" = "--verbose" ] && VERBOSE=1

MINGW_BIN="${MINGW_BIN:-$HOME/.local/cxge/toolchains/llvm-mingw/bin}"
CC=""
for c in "$MINGW_BIN/x86_64-w64-mingw32-clang" "$MINGW_BIN/x86_64-w64-mingw32-gcc" \
         "$(command -v x86_64-w64-mingw32-clang 2>/dev/null || true)" \
         "$(command -v x86_64-w64-mingw32-gcc 2>/dev/null || true)"; do
  [ -n "$c" ] && [ -x "$c" ] && { CC="$c"; break; }
done
[ -n "$CC" ] || {
  echo "error: no x86_64-w64-mingw32 compiler found." >&2
  echo "       set MINGW_BIN, or install llvm-mingw:" >&2
  echo "       https://github.com/mstorsjo/llvm-mingw/releases" >&2
  exit 2
}

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# The one thing that cannot be read off a binary: ng4-observe.c compiles either
# as a probe or, with this flag, as the shipped repair. The DLL that ships is
# the repair.
flags_for() {
  case "$1" in
    ng4-observe.c) echo "-DNG4_FIX" ;;
    *)             echo "" ;;
  esac
}

# Strings the linker invents rather than the source: build ids, PDB names.
# Filtered so a fresh stamp does not read as a changed fix.
meaningful() {
  LC_ALL=C strings -a "$1" \
    | grep -aE '^[ -~]{12,}$' \
    | grep -avE 'RSDS|LLD PDB|\.pdb$' \
    | sort -u
}

drifted=0
checked=0
missing_source=0

for dll in "$HERE"/*.dll; do
  name="$(basename "$dll")"

  stem="$(LC_ALL=C strings -a "$dll" | grep -aoE '^[A-Za-z0-9_]+_real\.' | head -1 | sed 's/_real\.$//')"
  mark="$(LC_ALL=C strings -a "$dll" | grep -aoE '[A-Za-z0-9_-]+\.log' | head -1)"
  if [ -z "$stem" ] || [ -z "$mark" ]; then
    printf '  %-32s ?  not a proxy of ours, or it names neither carrier nor log\n' "$name"
    continue
  fi

  src=""
  for d in runtime diagnostics; do
    for candidate in "$ROOT/$d"/*.c; do
      [ -f "$candidate" ] || continue
      LC_ALL=C grep -qaF "\"$mark\"" "$candidate" 2>/dev/null && { src="$candidate"; break 2; }
      LC_ALL=C grep -qaF "$mark" "$candidate" 2>/dev/null && { src="$candidate"; break 2; }
    done
  done
  if [ -z "$src" ]; then
    printf '  %-32s ?  names %s and no source writes that\n' "$name" "$mark"
    missing_source=$((missing_source + 1))
    continue
  fi

  out="$TMP/$name.d"; mkdir -p "$out"
  def="$out/$stem.def"
  {
    echo "LIBRARY $stem.dll"
    echo "EXPORTS"
    python3 "$PE" exports "$dll" --ordinals |
      while read -r ordinal sym; do
        printf '    %s = %s_real.%s @%s\n' "$sym" "$stem" "$sym" "$ordinal"
      done
  } > "$def"

  if ! "$CC" -shared -O2 $(flags_for "$(basename "$src")") -I"$HERE" \
        -o "$out/$stem.dll" "$src" "$def" \
        -Wl,--enable-stdcall-fixup \
        -lmfuuid -lole32 -luuid -lshlwapi -lkernel32 -static-libgcc 2>"$out/err"; then
    printf '  %-32s !! %s does not compile\n' "$name" "$(basename "$src")"
    [ "$VERBOSE" = 1 ] && sed 's/^/       /' "$out/err"
    drifted=$((drifted + 1)); continue
  fi

  checked=$((checked + 1))
  python3 "$PE" exports "$dll"           | sort > "$out/exp.old"
  python3 "$PE" exports "$out/$stem.dll" | sort > "$out/exp.new"
  meaningful "$dll"           > "$out/str.old"
  meaningful "$out/$stem.dll" > "$out/str.new"

  exp_delta="$(comm -3 "$out/exp.old" "$out/exp.new" | wc -l | tr -d ' ')"
  gone="$(comm -23 "$out/str.old" "$out/str.new")"
  came="$(comm -13 "$out/str.old" "$out/str.new")"
  n_gone="$(printf '%s' "$gone" | grep -c . )"
  n_came="$(printf '%s' "$came" | grep -c . )"

  if [ "$exp_delta" = 0 ] && [ "$n_gone" = 0 ] && [ "$n_came" = 0 ]; then
    printf '  %-32s ok   %s\n' "$name" "$(basename "$src")"
    continue
  fi

  drifted=$((drifted + 1))
  printf '  %-32s DRIFT  %s  (exports %s, strings -%s +%s)\n' \
    "$name" "$(basename "$src")" "$exp_delta" "$n_gone" "$n_came"
  if [ "$VERBOSE" = 1 ]; then
    printf '%s\n' "$gone" | grep . | head -8 | sed 's/^/       shipped only: /'
    printf '%s\n' "$came" | grep . | head -8 | sed 's/^/       source only:  /'
  fi
done

# The other copy of every one of these: the app runs the bundle's, not these.
echo
bundle="$ROOT/app/MacGameVideoFix.app/Contents/Resources"
bundle_drift=0
if [ -d "$bundle" ]; then
  for f in "$HERE"/*.dll "$HERE"/install-*.sh "$HERE"/stage-codecs.sh "$HERE"/pe.py; do
    [ -f "$f" ] || continue
    b="$bundle/$(basename "$f")"
    [ -f "$b" ] || continue
    cmp -s "$f" "$b" || { echo "  bundle stale: $(basename "$f")"; bundle_drift=$((bundle_drift + 1)); }
  done
  [ "$bundle_drift" = 0 ] && echo "  bundle: every copy matches runtime/"
else
  echo "  bundle: not built"
fi

echo
echo "  $checked rebuilt and compared, $drifted drifted, $missing_source unattributed"
[ "$drifted" = 0 ] && [ "$bundle_drift" = 0 ] && [ "$missing_source" = 0 ] || exit 1
exit 0
