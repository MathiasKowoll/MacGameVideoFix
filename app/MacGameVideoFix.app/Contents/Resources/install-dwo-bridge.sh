#!/usr/bin/env bash
#
# Install (or remove) the video bridge for DYNASTY WARRIORS: ORIGINS.
#
# Nothing the game ships is edited. One DLL is renamed and one is added:
#
#   libxess.dll       <- our proxy, forwards every symbol to libxess_real.dll
#   libxess_real.dll  <- the game's original, untouched
#
# libxess is Intel's XeSS upscaler. It carries the fix because the game loads
# it directly and it has nothing to do with video, so a proxy in front of it
# cannot disturb anything it does.
#
#   install-dwo-bridge.sh <game folder>            install
#   install-dwo-bridge.sh <game folder> --restore  remove
#   install-dwo-bridge.sh <game folder> --status   report what is in place
#
# <game folder> is the one holding DWORIGINS.exe, e.g.
#   .../steamapps/common/DWORIGINS
#
# CrossOver must be patched with winevideo: this presents frames, it does not
# decode them, and VP9 in a WebM container does not come out of Media
# Foundation without it.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROXY="${PROXY_DLL:-$HERE/libxess.dll}"
EXPORTS="$HERE/pe.py"

# The proxy writes this path at runtime; finding it inside a DLL is how we tell
# our file apart from Intel's.
MARKER='dwo-video-bridge.log'

usage() { sed -n '3,26p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"

[ -f "$GAME/DWORIGINS.exe" ] || {
  echo "error: no DWORIGINS.exe in $GAME" >&2
  echo "       Point this at the folder holding the game's executable." >&2
  exit 1
}

LIVE="$GAME/libxess.dll"
REAL="$GAME/libxess_real.dll"

[ -f "$LIVE" ] || [ -f "$REAL" ] || {
  echo "error: no libxess.dll in $GAME" >&2
  echo "       This build of the game has no carrier for the bridge to ride on." >&2
  exit 1
}

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

status() {
  if is_ours "$LIVE" && [ -f "$REAL" ]; then echo installed
  elif is_ours "$LIVE"; then echo broken       # proxy present, original missing
  else echo absent
  fi
}

case "$MODE" in
--status)
  echo "$(status) $GAME"
  exit 0
  ;;

--restore)
  state="$(status)"
  if [ "$state" = absent ]; then
    echo "the bridge is not installed, nothing to do"
    exit 0
  fi
  if [ "$state" = broken ]; then
    echo "error: $LIVE is our proxy but $REAL is missing." >&2
    echo "       Verify the game files in Steam to get the original back." >&2
    exit 1
  fi
  echo "[1/2] restoring the original libxess.dll"
  mv -f "$REAL" "$LIVE"
  echo "[2/2] done — the game is back to stock"
  ;;

--install|"")
  [ -f "$PROXY" ] || { echo "error: proxy DLL not found at $PROXY" >&2; exit 1; }

  if [ "$(status)" = installed ]; then
    echo "the bridge is already installed, nothing to do"
    exit 0
  fi

  echo "[1/4] checking libxess.dll"
  # A game update or a Steam verification puts the stock DLL back, which leaves
  # any libxess_real.dll behind it stale.
  if is_ours "$LIVE"; then
    echo "error: $LIVE is already a proxy but $REAL is gone." >&2
    echo "       Verify the game files in Steam, then run this again." >&2
    exit 1
  fi

  echo "[2/4] checking the proxy exports everything the game imports"
  # A missing entry point would stop the game starting at all, so refuse now
  # rather than let that happen.
  missing="$(comm -23 <(python3 "$EXPORTS" exports "$LIVE" | sort) \
                      <(python3 "$EXPORTS" exports "$PROXY" | sort))"
  if [ -n "$missing" ]; then
    echo "error: this build's libxess exports symbols the shipped proxy does not:" >&2
    echo "$missing" | sed 's/^/       /' >&2
    echo "       Rebuild it: runtime/build-proxy.sh \"$LIVE\" dwo-video-bridge.c" >&2
    exit 1
  fi

  echo "[3/4] moving the original aside as libxess_real.dll"
  mv -f "$LIVE" "$REAL"

  echo "[4/4] installing the bridge"
  cp "$PROXY" "$LIVE"

  echo
  echo "installed — the cutscenes should play"
  echo "CrossOver must be patched with winevideo, or there is nothing to decode them"
  echo "a log is written to the bottle's C:\\${MARKER} on each launch"
  ;;

*)
  usage
  ;;
esac
