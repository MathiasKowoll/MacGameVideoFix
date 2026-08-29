#!/usr/bin/env bash
#
# Install the Nioh video bridge. Takes Nioh or Nioh 2: the two ship the same
# carrier, the same WMV3-in-ASF video, and the same pair of d3d9 and d3d11
# imports. They reach the video differently -- Nioh builds a DirectShow graph,
# Nioh 2 goes through Media Foundation -- and the bridge does not need to know
# which, since it watches the shared surface rather than the player.
#
#     install-nioh-bridge.sh <game folder>            install
#     install-nioh-bridge.sh <game folder> --status   report
#     install-nioh-bridge.sh <game folder> --restore  undo
#
# Same bridge as Persona 5 Strikers, same binary logic, different carrier: it
# rides on GfeSDK.dll, NVIDIA's GeForce Experience SDK, which the game imports
# and which does nothing at all under Metal. steam_api64.dll was the other
# candidate and was rejected: nothing here re-exports a Steamworks entry point.
#
# This game also needs a WMV3 decoder CrossOver does not ship. See
# runtime/stage-codecs.sh; the bridge alone will not make the video appear.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# WHAT THIS SCRIPT IS FOR, in a form something other than a human can read.
#
# One MGVF-GAME line per title this installer serves -- four of them serve more
# than one, which the manifest could not express before. The fields are the
# game's name, its shipping executable, and where the carrier sits relative to
# the game folder (empty means the folder itself). The executable is the
# identity: there is no AppID anywhere in this project, and the folder name is
# Valve's to choose -- Mortal Shell 2 installs into one called Sparta.
#
# runtime/check-builds.sh checks these against the app's own table, so the two
# copies cannot drift apart in silence.
#
# MGVF-SCOPE: folder
# MGVF-GAME: Nioh | nioh.exe | 
# MGVF-GAME: Nioh 2 | nioh2.exe | 
# MGVF-WHY: Cutscene runs with sound and no picture. Needs the staged decoder as well: its video is WMV3.

set -euo pipefail

usage() { sed -n '3,21p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
# A read-only caller (scripts/support-bundle.sh) sets MGVF_STATUS_ONLY=1. The
# default above is the DESTRUCTIVE branch, so without this the read-only
# property of a whole support bundle rests on the literal --status never being
# lost from one line of one other script. Structural beats positional.
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

LIVE="$GAME/GfeSDK.dll"
REAL="$GAME/GfeSDK_real.dll"
PROXY="$HERE/GfeSDK.dll"
EXPORTS="$HERE/pe.pl"
# Built from the same source as the Strikers bridge, so it carries the same
# log name -- which is what identifies one of ours.
MARKER='p5s-video-bridge.log'

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

# Plain if rather than [ -f ] && assignment: under set -e a failing test as the
# last command of the loop body would end the script here.
GAME_EXE=""
for e in nioh.exe nioh2.exe; do
  if [ -f "$GAME/$e" ]; then GAME_EXE="$e"; fi
done
[ -n "$GAME_EXE" ] || {
  echo "error: no nioh.exe or nioh2.exe in $GAME" >&2
  echo "       Pick the folder Nioh or Nioh 2 is installed in." >&2
  exit 1
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
  if [ -f "$REAL" ]; then
    mv -f "$REAL" "$LIVE"
    echo "restored — the game is back to its own GfeSDK.dll"
  else
    echo "nothing to restore"
  fi
  exit 0
  ;;
--install) ;;
*) usage ;;
esac

if is_ours "$LIVE" && [ -f "$REAL" ]; then
  echo "the bridge is already installed, nothing to do"
  exit 0
fi

echo "[1/3] checking GfeSDK.dll"
if is_ours "$LIVE"; then
  echo "error: $LIVE is already a proxy but $REAL is gone." >&2
  echo "       Verify the game files in Steam, then run this again." >&2
  exit 1
fi
[ -f "$LIVE" ] || {
  echo "error: no GfeSDK.dll in $GAME" >&2
  echo "       This build of the game has no carrier for the bridge to ride on." >&2
  exit 1
}

echo "[2/3] checking the proxy forwards everything the game imports"
# Read each side separately: piping into comm hides a failure, and an
# unreadable file would then be moved over the saved original.
if ! live_exports="$(/usr/bin/perl "$EXPORTS" exports "$LIVE" 2>&1)"; then
  echo "error: cannot read the exports of $LIVE" >&2
  echo "$live_exports" | sed 's/^/       /' >&2
  [ -f "$REAL" ] && echo "       Your original is beside it; restore before anything else." >&2
  exit 1
fi
if ! proxy_exports="$(/usr/bin/perl "$EXPORTS" exports "$PROXY" 2>&1)"; then
  echo "error: cannot read the exports of $PROXY" >&2
  exit 1
fi
missing="$(comm -23 <(printf '%s\n' "$live_exports" | sort) \
                    <(printf '%s\n' "$proxy_exports" | sort))"
if [ -n "$missing" ]; then
  echo "error: this build's GfeSDK exports symbols the shipped proxy does not:" >&2
  echo "$missing" | sed 's/^/       /' >&2
  exit 1
fi

echo "[3/3] installing"
# Nothing may write over $LIVE until the original is known to be safe under
# $REAL. Without this the two commands are independent: on a directory that
# denies rename but leaves the file writable the mv fails, the cp truncates the
# original in place, and the script exits 0 printing "installed".
mv -f "$LIVE" "$REAL" || {
  echo "error: could not move the original aside; nothing was changed" >&2
  exit 1
}
[ -f "$REAL" ] || {
  echo "error: the original is not where it should be; refusing to write" >&2
  exit 1
}
cp "$PROXY" "$LIVE" || {
  echo "error: could not install the bridge; putting the original back" >&2
  mv -f "$REAL" "$LIVE" || echo "       and that failed too -- it is at $REAL" >&2
  exit 1
}
echo
echo "installed"
echo "  the video bridge is in place for $GAME_EXE"
if [ -n "${MGVF_FRONTEND:-}" ]; then
  echo "  this game also needs the WMV3 codec -- use the Stage codec button"
else
  echo "  this game also needs the staged codec -- runtime/stage-codecs.sh"
fi
