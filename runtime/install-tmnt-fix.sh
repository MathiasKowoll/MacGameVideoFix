#!/usr/bin/env bash
#
# Install the TEENAGE MUTANT NINJA TURTLES: SPLINTERED FATE startup fix.
#
#     install-tmnt-fix.sh <game folder>            install
#     install-tmnt-fix.sh <game folder> --status   report
#     install-tmnt-fix.sh <game folder> --restore  undo
#
# The folder is the one holding TMNTSF.exe.
#
# WHAT IT FIXES. The game opens a window and dies about three seconds later,
# silently -- no dialog, no Wine backtrace, nothing in any log. It asks D3D12
# whether the first shader it loads carries an embedded root signature, which
# is an ordinary thing to ask. On Windows the answer for a shader that has none
# is E_INVALIDARG. Under D3DMetal that call reads a field at offset 4 of the
# part it did not find, and the process ends.
#
# This checks the container first and gives the answer Windows gives. When a
# container really does hold a root signature the call goes through untouched.
#
# THE CARRIER IS THE GAME'S OWN. fmod.dll is audio, the game imports it
# directly, and nothing is redistributed: its copy is renamed fmod_real.dll and
# every one of its 1109 exports is forwarded straight back to it. No registry
# key is written and no CrossOver file is copied -- unlike the bridges, this
# rides on a DLL the game already ships.
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
# MGVF-GAME: TMNT: Splintered Fate | TMNTSF.exe | 
# MGVF-WHY: Not about video: it guards one D3D12 call that ends the process instead of returning an error.

set -euo pipefail

usage() { sed -n '3,27p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

EXE_NAME='TMNTSF.exe'
LIVE="$GAME/fmod.dll"
REAL="$GAME/fmod_real.dll"
PROXY="$HERE/fmod-tmnt.dll"
EXPORTS="$HERE/pe.pl"
# Two ways to recognise our own proxy, because one of them has already gone
# stale once: the log path moved when rootsig-guard.c became d3d12-guards.c and
# this string was not updated with it, which made is_ours() answer no about a
# DLL that was ours. What that costs is at the bottom of this file, in the
# refusal to move a proxy onto the saved original.
MARKERS='d3d12-guards.log rootsig-guard.log'

is_ours() {
  [ -f "$1" ] || return 1
  for m in $MARKERS; do LC_ALL=C grep -qa "$m" "$1" && return 0; done
  return 1
}

# The same question asked a way that cannot go stale: our proxy forwards every
# export to fmod_real, so the EXPORT table carries one "fmod_real.<symbol>"
# string per forwarder. A genuine library never forwards to its own _real
# variant. Markers are for reporting; this is what the destructive step is
# allowed to rely on.
#
# The pattern is the prefix with its dot and NOT "fmod_real.dll", which is a
# string that never appears anywhere in the file: build-proxy.sh emits pure
# forwarders and no import descriptor, so the old test could not match, ever.
# It degenerated into is_ours, the marker check that :44-48 already admits went
# stale once -- leaving the mv below with no real guard in front of it.
looks_like_ours() {
  [ -f "$1" ] || return 1
  is_ours "$1" && return 0
  LC_ALL=C grep -qaF "fmod_real." "$1"
}

[ -f "$GAME/$EXE_NAME" ] || {
  echo "error: no '$EXE_NAME' in $GAME" >&2
  echo "       Pick the folder Splintered Fate is installed in." >&2
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
  # mv -f overwrites; removing $LIVE first only opens a window in which neither
  # file is in place, and an interruption there left a state nothing recovered.
  if [ -f "$REAL" ] && ! looks_like_ours "$REAL" \
       && { looks_like_ours "$LIVE" || [ ! -e "$LIVE" ]; }; then
    mv -f "$REAL" "$LIVE"
    echo "restored — the game is back to its own fmod.dll"
  elif looks_like_ours "$LIVE"; then
    # Our proxy is here and the game's own DLL is not saved anywhere. There is
    # nothing to put back, and saying "nothing of ours is installed" about a
    # file that is plainly ours sends the reader looking in the wrong place.
    echo "our proxy is installed, but the game's own fmod.dll is not saved here." >&2
    echo "  Verify the game files in Steam to get it back, then run --install." >&2
    exit 1
  else
    echo "nothing of ours is installed"
  fi
  exit 0
  ;;
--install) ;;
*) usage ;;
esac

if is_ours "$LIVE" && [ -f "$REAL" ]; then
  echo "the fix is already installed, nothing to do"
  exit 0
fi

echo "[1/3] checking fmod.dll"
[ -f "$LIVE" ] || { echo "error: no fmod.dll in $GAME" >&2; exit 1; }
# Never over a proxy: if $LIVE is already ours, $REAL would be overwritten with
# the proxy and the original lost for good.
if is_ours "$LIVE"; then
  echo "error: $LIVE is already a proxy but $REAL is gone." >&2
  echo "       Verify the game files in Steam, then run this again." >&2
  exit 1
fi

echo "[2/3] checking the proxy forwards everything the original exports"
if ! real_exports="$(/usr/bin/perl "$EXPORTS" exports "$LIVE" 2>&1)"; then
  echo "error: cannot read the exports of $LIVE" >&2; exit 1
fi
if ! proxy_exports="$(/usr/bin/perl "$EXPORTS" exports "$PROXY" 2>&1)"; then
  echo "error: cannot read the exports of $PROXY" >&2; exit 1
fi
missing="$(comm -23 <(printf '%s\n' "$real_exports" | sort) \
                    <(printf '%s\n' "$proxy_exports" | sort))"
if [ -n "$missing" ]; then
  echo "error: this build's fmod exports symbols the shipped proxy does not:" >&2
  echo "$missing" | head -8 | sed 's/^/       /' >&2
  echo "       The game has been updated; rebuild the proxy against it." >&2
  exit 1
fi

echo "[3/3] installing"
# What must never happen is moving a PROXY onto the saved original: that
# destroys the only copy of the game's own DLL. What is merely untidy is a
# leftover $REAL sitting beside a genuine $LIVE, which is exactly what a Steam
# file verification or a game patch leaves behind -- there the genuine library
# is the live one, and saving it over the stale copy is the right move.
#
# An earlier version of this refused on "$REAL exists" alone. That is the wrong
# question: it locked the ordinary post-verification state out of both install
# and restore, with no way back.
if looks_like_ours "$LIVE"; then
  echo "error: $LIVE is already a proxy, so the game's own DLL is not here to save." >&2
  echo "       Run --restore, or verify the game files in Steam, then try again." >&2
  exit 1
fi
if [ -e "$REAL" ]; then
  echo "  $REAL was left over from before; $LIVE is the game's own, so it replaces it"
fi
mv -f "$LIVE" "$REAL"
cp "$PROXY" "$LIVE" || { mv -f "$REAL" "$LIVE"; echo "error: could not install" >&2; exit 1; }
echo
echo "installed"
echo "  the game's own fmod.dll is now fmod_real.dll and every export reaches it"
echo "  no registry key was written and no CrossOver file was copied"
