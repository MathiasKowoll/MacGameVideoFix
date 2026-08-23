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
EXPORTS="$HERE/pe.py"
MARKER='rootsig-guard.log'

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

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
  if is_ours "$LIVE" && [ -f "$REAL" ]; then
    rm -f "$LIVE"; mv -f "$REAL" "$LIVE"
    echo "restored — the game is back to its own fmod.dll"
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
if ! real_exports="$(python3 "$EXPORTS" exports "$LIVE" 2>&1)"; then
  echo "error: cannot read the exports of $LIVE" >&2; exit 1
fi
if ! proxy_exports="$(python3 "$EXPORTS" exports "$PROXY" 2>&1)"; then
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
mv -f "$LIVE" "$REAL"
cp "$PROXY" "$LIVE" || { mv -f "$REAL" "$LIVE"; echo "error: could not install" >&2; exit 1; }
echo
echo "installed"
echo "  the game's own fmod.dll is now fmod_real.dll and every export reaches it"
echo "  no registry key was written and no CrossOver file was copied"
