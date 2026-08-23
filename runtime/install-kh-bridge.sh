#!/usr/bin/env bash
#
# Install the KINGDOM HEARTS video bridge.
#
#     install-kh-bridge.sh <game folder>            install
#     install-kh-bridge.sh <game folder> --status   report
#     install-kh-bridge.sh <game folder> --restore  undo
#
# Two packages take this fix, and between them eight executables play video:
#
#   HD 2.8 Final Chapter Prologue   Dream Drop Distance, and the launcher,
#                                   which is what plays chi Back Cover
#   HD 1.5+2.5 ReMIX                FINAL MIX, Re_Chain of Memories,
#                                   II FINAL MIX, Birth by Sleep FINAL MIX,
#                                   the Theater and the launcher
#
# Give it the package folder -- the one Steam installed, holding those
# executables. It works out which are there.
#
# 0.2 Birth by Sleep ships inside 2.8 and is not in the list: its cutscenes are
# software MPEG-1 through CriWare, they already play, and this does nothing for
# them. KINGDOM HEARTS III is the same case and is not supported here either.
#
# THE CARRIER IS NOT THE GAME'S. The only DLL beside these executables is
# steam_api64.dll, and nothing here rides on Steam's API or re-exports a
# Steamworks entry point. So the bridge rides on dinput8.dll, which every one of
# them imports and which has five exports and nothing to do with rendering. The
# original is CrossOver's own: this script copies it out of your bottle and
# beside the game as dinput8_real.dll. Nothing is redistributed -- the copy is
# your file -- but it is a copy, so re-run this after a CrossOver upgrade if
# input ever misbehaves.
#
# IT WRITES ONE REGISTRY KEY PER EXECUTABLE. Wine implements dinput8 itself and
# prefers its own build, so a DLL sitting beside the game is never loaded. The
# override says otherwise, and is scoped to those executables alone: no other
# title in the bottle sees it. --restore removes them.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

usage() { sed -n '3,36p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

# Every executable in either package that opens a source reader. Checked
# against the folder, so only the ones actually there are touched.
KNOWN=(
  'KINGDOM HEARTS Dream Drop Distance.exe'
  'KINGDOM HEARTS HD 2.8 Launcher.exe'
  'KINGDOM HEARTS FINAL MIX.exe'
  'KINGDOM HEARTS Re_Chain of Memories.exe'
  'KINGDOM HEARTS II FINAL MIX.exe'
  'KINGDOM HEARTS Birth by Sleep FINAL MIX.exe'
  'KINGDOM HEARTS Theater.exe'
  'KINGDOM HEARTS HD 1.5+2.5 Launcher.exe'
)

EXE_NAMES=()
for e in "${KNOWN[@]}"; do
  [ -f "$GAME/$e" ] && EXE_NAMES+=("$e")
done

LIVE="$GAME/dinput8.dll"
REAL="$GAME/dinput8_real.dll"
PROXY="$HERE/dinput8-kh.dll"
EXPORTS="$HERE/pe.py"
MARKER='dwo-video-bridge.log'

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

[ ${#EXE_NAMES[@]} -gt 0 ] || {
  echo "error: no KINGDOM HEARTS executable found in $GAME" >&2
  echo "       Pick the package folder -- HD 2.8 Final Chapter Prologue, or" >&2
  echo "       HD 1.5+2.5 ReMIX -- the one holding the game's .exe files." >&2
  exit 1
}

BOTTLES="$HOME/Library/Application Support/CrossOver/Bottles"

find_bottles() {
  local b vdf lib key hit=0
  lib="${GAME%/steamapps/common/*}"
  key="$(printf '%s' "${lib#/}" | tr -d '/\\' | tr '[:upper:]' '[:lower:]')"
  if [ "$lib" != "$GAME" ] && [ -n "$key" ]; then
    for b in "$BOTTLES"/*/; do
      [ -f "$b/drive_c/windows/system32/dinput8.dll" ] || continue
      vdf="$(find "$b/drive_c" -maxdepth 7 -iname libraryfolders.vdf 2>/dev/null | head -1)"
      [ -n "$vdf" ] || continue
      LC_ALL=C tr -d '/\\' < "$vdf" | tr '[:upper:]' '[:lower:]' \
        | LC_ALL=C grep -qaF "$key" || continue
      printf '%s\n' "${b%/}"; hit=1
    done
  fi
  [ "$hit" = 1 ] && return 0
  for b in "$BOTTLES"/*/; do
    [ -f "$b/drive_c/windows/system32/dinput8.dll" ] || continue
    printf '%s\n' "${b%/}"; return 0
  done
  return 1
}

find_bottle() {
  local out
  out="$(find_bottles)" || return 1
  [ -n "$out" ] || return 1
  printf '%s' "${out%%$'\n'*}"
}

find_crossover() {
  local a
  for a in "/Applications/CrossOver Preview.app" "/Applications/CrossOver.app"; do
    [ -x "$a/Contents/SharedSupport/CrossOver/bin/wine" ] || continue
    printf '%s' "$a/Contents/SharedSupport/CrossOver"; return 0
  done
  return 1
}

case "$MODE" in
--status)
  if is_ours "$LIVE" && [ -f "$REAL" ]; then echo installed
  elif is_ours "$LIVE"; then echo broken
  elif [ ! -f "$LIVE" ] && [ -f "$REAL" ]; then echo half
  else echo absent; fi
  exit 0
  ;;
--restore)
  rm -f "$LIVE" "$REAL"
  if CX="$(find_crossover)"; then
    while read -r b; do
      [ -n "$b" ] || continue
      for exe in "${EXE_NAMES[@]}"; do
        "$CX/bin/wine" --bottle "$(basename "$b")" --cx-app reg.exe delete \
          "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$exe\\DllOverrides" \
          /v dinput8 /f >/dev/null 2>&1 || true
      done
    done < <(find_bottles || true)
  fi
  echo "restored — the bridge and the dinput8 overrides are gone"
  exit 0
  ;;
--install) ;;
*) usage ;;
esac

if is_ours "$LIVE" && [ -f "$REAL" ]; then
  echo "the bridge is already installed, nothing to do"
  exit 0
fi

echo "[1/4] finding the bottle and the CrossOver that runs it"
BOTTLE="$(find_bottle)" || {
  echo "error: no CrossOver bottle with a dinput8.dll was found" >&2
  echo "       Run the game once first, so its bottle exists." >&2
  exit 1
}
CX="$(find_crossover)" || {
  echo "error: no CrossOver installation was found in /Applications" >&2
  exit 1
}
echo "      bottle: $(basename "$BOTTLE")"
echo "      ${#EXE_NAMES[@]} executable(s) that play video in this package"

echo "[2/4] taking a copy of the bottle's own dinput8"
if is_ours "$LIVE"; then
  echo "error: $LIVE is already a proxy but $REAL is gone." >&2
  echo "       Verify the game files in Steam, then run this again." >&2
  exit 1
fi
cp "$BOTTLE/drive_c/windows/system32/dinput8.dll" "$REAL" || {
  echo "error: could not copy the original beside the game" >&2
  exit 1
}

echo "[3/4] checking the proxy forwards everything the original exports"
if ! real_exports="$(python3 "$EXPORTS" exports "$REAL" 2>&1)"; then
  echo "error: cannot read the exports of $REAL" >&2; rm -f "$REAL"; exit 1
fi
if ! proxy_exports="$(python3 "$EXPORTS" exports "$PROXY" 2>&1)"; then
  echo "error: cannot read the exports of $PROXY" >&2; rm -f "$REAL"; exit 1
fi
missing="$(comm -23 <(printf '%s\n' "$real_exports" | sort) \
                    <(printf '%s\n' "$proxy_exports" | sort))"
if [ -n "$missing" ]; then
  echo "error: this CrossOver's dinput8 exports symbols the shipped proxy does not:" >&2
  echo "$missing" | sed 's/^/       /' >&2
  echo "       Rebuild the proxy against it; nothing was installed." >&2
  rm -f "$REAL"
  exit 1
fi
cp "$PROXY" "$LIVE" || { echo "error: could not install the bridge" >&2; rm -f "$REAL"; exit 1; }

echo "[4/4] telling Wine to prefer it, for these executables only"
wrote=0
while read -r b; do
  [ -n "$b" ] || continue
  ok=0
  for exe in "${EXE_NAMES[@]}"; do
    "$CX/bin/wine" --bottle "$(basename "$b")" --cx-app reg.exe add \
      "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$exe\\DllOverrides" \
      /v dinput8 /d "native,builtin" /f >/dev/null 2>&1 || continue
    # Ask the registry, not the file. wineserver flushes user.reg on its own
    # schedule, so a key that was just written is often not on disk yet -- and
    # reading the file makes a lazy flush look like a failed write.
    "$CX/bin/wine" --bottle "$(basename "$b")" --cx-app reg.exe query \
      "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$exe\\DllOverrides" \
      /v dinput8 >/dev/null 2>&1 && ok=1
  done
  [ "$ok" = 1 ] || continue
  echo "      $(basename "$b")"
  wrote=$((wrote + 1))
done < <(find_bottles || true)
if [ "$wrote" = 0 ]; then
  echo "error: the registry overrides could not be written to any bottle." >&2
  echo "       Without them Wine loads its own dinput8 and the bridge never runs." >&2
  rm -f "$LIVE"; mv -f "$REAL" "$LIVE" 2>/dev/null || true
  exit 1
fi
echo
echo "installed"
echo "  the video bridge is in place, and dinput8 is overridden for this"
echo "  package's executables only"
echo "  no staged codec is needed for this one"
