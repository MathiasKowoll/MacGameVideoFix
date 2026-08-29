#!/usr/bin/env bash
#
# Install the Nioh 3 video bridge.
#
#     install-nioh3-bridge.sh <game folder>            install
#     install-nioh3-bridge.sh <game folder> --status   report
#     install-nioh3-bridge.sh <game folder> --restore  undo
#
# Separate from install-nioh-bridge.sh despite the family name, because almost
# nothing is shared. Nioh and Nioh 2 are DXMT titles fixed by the Persona 5
# Strikers bridge riding on GfeSDK.dll. Nioh 3 is D3D12 on D3DMetal and is
# fixed by the DYNASTY WARRIORS bridge riding on amd_ags_x64.dll. Same series,
# different fault, different code.
#
# The carrier name collides with Persona 5 Strikers', which uses a different
# bridge -- so the shipped proxy is kept as amd_ags_x64-nioh3.dll and only
# takes the plain name once it is in the game's folder. The two games' AGS
# builds do not even export the same symbols.
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
# MGVF-GAME: Nioh 3 | Nioh3.exe | 
# MGVF-WHY: Cutscene runs with sound, picture black. Same bridge as DYNASTY WARRIORS on a different carrier.

set -euo pipefail

usage() { sed -n '3,20p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
# A read-only caller (scripts/support-bundle.sh) sets MGVF_STATUS_ONLY=1. The
# default above is the DESTRUCTIVE branch, so without this the read-only
# property of a whole support bundle rests on the literal --status never being
# lost from one line of one other script. Structural beats positional.
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

LIVE="$GAME/amd_ags_x64.dll"
REAL="$GAME/amd_ags_x64_real.dll"
PROXY="$HERE/amd_ags_x64-nioh3.dll"
EXPORTS="$HERE/pe.pl"
MARKER='dwo-video-bridge.log'

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

[ -f "$GAME/Nioh3.exe" ] || {
  echo "error: no Nioh3.exe in $GAME" >&2
  echo "       Pick the folder Nioh 3 is installed in." >&2
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
    echo "restored — the game is back to its own amd_ags_x64.dll"
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

echo "[1/3] checking amd_ags_x64.dll"
if is_ours "$LIVE"; then
  echo "error: $LIVE is already a proxy but $REAL is gone." >&2
  echo "       Verify the game files in Steam, then run this again." >&2
  exit 1
fi
[ -f "$LIVE" ] || {
  echo "error: no amd_ags_x64.dll in $GAME" >&2
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
  echo "error: this build's amd_ags exports symbols the shipped proxy does not:" >&2
  echo "$missing" | sed 's/^/       /' >&2
  echo "       This is the check that stops Persona 5 Strikers' proxy being" >&2
  echo "       installed here: the two games ship different AGS builds." >&2
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
echo "  the video bridge is in place"
echo "  no staged codec is needed for this one"
