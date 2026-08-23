#!/usr/bin/env bash
#
# Install the KINGDOM HEARTS Dream Drop Distance video bridge.
#
#     install-kh3d-bridge.sh <game folder>            install
#     install-kh3d-bridge.sh <game folder> --status   report
#     install-kh3d-bridge.sh <game folder> --restore  undo
#
# The folder is the KINGDOM HEARTS HD 2.8 Final Chapter Prologue one -- the
# folder holding "KINGDOM HEARTS Dream Drop Distance.exe", not the launcher's
# parent and not the 0.2 Birth by Sleep subfolder.
#
# This fixes Dream Drop Distance only. 0.2 Birth by Sleep ships in the same
# package but plays its cutscenes through CriWare Sofdec (.usm), which never
# touches Media Foundation, so this bridge has nothing to do there.
#
# THE CARRIER IS NOT THE GAME'S, as with NieR. The only DLL beside the
# executable is steam_api64.dll, and nothing here rides on Steam's API or
# re-exports a Steamworks entry point. So the bridge rides on dinput8.dll,
# which the game imports and which has five exports and nothing to do with
# rendering. The original is CrossOver's own: this script copies it out of your
# bottle and beside the game as dinput8_real.dll. Nothing is redistributed --
# the copy is your file -- but it is a copy, so re-run this after a CrossOver
# upgrade if input ever misbehaves.
#
# IT WRITES ONE REGISTRY KEY. Wine implements dinput8 itself and prefers its
# own build, so a DLL sitting beside the game is never loaded. The override
# below says otherwise, and is scoped to this executable alone: no other title
# in the bottle sees it. --restore removes it.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

usage() { sed -n '3,30p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

EXE_NAME='KINGDOM HEARTS Dream Drop Distance.exe'
LIVE="$GAME/dinput8.dll"
REAL="$GAME/dinput8_real.dll"
PROXY="$HERE/dinput8-kh3d.dll"
EXPORTS="$HERE/pe.py"
MARKER='dwo-video-bridge.log'

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

[ -f "$GAME/$EXE_NAME" ] || {
  echo "error: no '$EXE_NAME' in $GAME" >&2
  echo "       Pick the KINGDOM HEARTS HD 2.8 Final Chapter Prologue folder." >&2
  exit 1
}

BOTTLES="$HOME/Library/Application Support/CrossOver/Bottles"

# Which bottles can actually run this copy of the game.
#
# Picking the first bottle that happens to have a dinput8.dll is wrong: a Mac
# can hold several, and the override has to land in the one the game is
# launched from. So match on the Steam library the game sits in -- every bottle
# whose libraryfolders.vdf lists it is a candidate, and each gets the override.
# Naming them all is deliberate: the user may switch bottles between runs, and
# an override for one executable is inert in a bottle that never runs it.
find_bottles() {
  local b vdf lib key hit=0
  # The library root, as a slash-free lowercase key: libraryfolders.vdf writes
  # it as Z:\Volumes\Disk\Library, doubling every separator, so compare with
  # all separators removed and the escaping stops mattering.
  lib="${GAME%/steamapps/common/*}"
  key="$(printf '%s' "${lib#/}" | tr -d '/\' | tr '[:upper:]' '[:lower:]')"
  if [ "$lib" != "$GAME" ] && [ -n "$key" ]; then
    for b in "$BOTTLES"/*/; do
      [ -f "$b/drive_c/windows/system32/dinput8.dll" ] || continue
      vdf="$(find "$b/drive_c" -maxdepth 7 -iname libraryfolders.vdf 2>/dev/null | head -1)"
      [ -n "$vdf" ] || continue
      LC_ALL=C tr -d '/\' < "$vdf" | tr '[:upper:]' '[:lower:]' \
        | LC_ALL=C grep -qaF "$key" || continue
      printf '%s
' "${b%/}"; hit=1
    done
  fi
  [ "$hit" = 1 ] && return 0
  # Not a Steam layout, or no bottle claims the library: fall back to any
  # bottle that could supply a dinput8 at all.
  for b in "$BOTTLES"/*/; do
    [ -f "$b/drive_c/windows/system32/dinput8.dll" ] || continue
    printf '%s
' "${b%/}"; return 0
  done
  return 1
}
# The first of them, for the things that need exactly one: the copy of the
# original dinput8. Not "find_bottles | head -1" -- head closes the pipe, and
# under pipefail the SIGPIPE that follows reads as a failure to find anything.
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
      "$CX/bin/wine" --bottle "$(basename "$b")" --cx-app reg.exe delete \
        "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$EXE_NAME\\DllOverrides" \
        /v dinput8 /f >/dev/null 2>&1 || true
    done < <(find_bottles || true)
  fi
  echo "restored — the bridge and the dinput8 override are gone"
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

echo "[4/4] telling Wine to prefer it, for this game only"
wrote=0
while read -r b; do
  [ -n "$b" ] || continue
  "$CX/bin/wine" --bottle "$(basename "$b")" --cx-app reg.exe add \
    "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$EXE_NAME\\DllOverrides" \
    /v dinput8 /d "native,builtin" /f >/dev/null 2>&1 || continue
  LC_ALL=C grep -qa "$EXE_NAME" "$b/user.reg" 2>/dev/null || continue
  echo "      $(basename "$b")"
  wrote=$((wrote + 1))
done < <(find_bottles || true)
if [ "$wrote" = 0 ]; then
  echo "error: the registry override could not be written to any bottle." >&2
  echo "       Without it Wine loads its own dinput8 and the bridge never runs." >&2
  rm -f "$LIVE"; mv -f "$REAL" "$LIVE" 2>/dev/null || true
  exit 1
fi
echo
echo "installed"
echo "  the video bridge is in place, and dinput8 is overridden for this game only"
echo "  no staged codec is needed for this one"
