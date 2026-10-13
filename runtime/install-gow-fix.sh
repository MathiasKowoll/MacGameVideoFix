#!/usr/bin/env bash
#
# Install the God of War (2018) depth fix.
#
#     install-gow-fix.sh <game folder>            install
#     install-gow-fix.sh <game folder> --status   report
#     install-gow-fix.sh <game folder> --restore  undo
#
# White specks cover the characters in dim light: the G-buffer pass tests depth
# with EQUAL against a prepass, and the translated shaders do not reproduce the
# prepass depth to the last bit. The fix creates that depth test as
# GREATER_EQUAL; see gow-depth-fix.c. Measured on DXMT under Rosetta and under
# FEX: specks without it, none with it.
#
# It rides on libSceJobManager.dll, which the game imports and no runner ships,
# so no DLL override is needed under any launcher, and nothing is copied out of
# CrossOver: a DXMT or D3DMetal update needs nothing done here.
#
# gow-depth-fix.ini beside the game says what an EQUAL test becomes:
# greater_equal (the fix, written by default), less_equal, or off. A value
# someone chose is kept across reinstalls.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# MGVF-SCOPE: folder
# MGVF-GAME: God of War | GoW.exe |
# MGVF-WHY: White specks on characters in dim light: an EQUAL depth test after a prepass, which the translated shaders do not match bit for bit.

set -euo pipefail

usage() { sed -n '3,7p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
# See install-p5s-bridge.sh: a read-only caller must never land on --install.
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

LIVE="$GAME/libSceJobManager.dll"
REAL="$GAME/libSceJobManager_real.dll"
PROXY="$HERE/libSceJobManager-gow.dll"
EXPORTS="$HERE/pe.pl"
INI="$GAME/gow-depth-fix.ini"
MARKER='gow-depth-fix.log'

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

[ -f "$GAME/GoW.exe" ] || {
  echo "error: no GoW.exe in $GAME" >&2
  echo "       Pick the folder God of War is installed in (usually GodOfWar)." >&2
  exit 1
}

case "$MODE" in
--status)
  if is_ours "$LIVE" && [ -f "$REAL" ]; then
    # An older build of ours still works, but installing again brings the
    # current one; the app knows four words and "broken" means "apply again".
    if cmp -s "$LIVE" "$PROXY"; then echo installed
    else echo "an older build of the fix is in place; install again" >&2; echo broken; fi
  elif is_ours "$LIVE"; then echo broken
  elif [ ! -f "$LIVE" ] && [ -f "$REAL" ]; then echo half
  else echo absent; fi
  exit 0
  ;;
--restore)
  if [ -f "$REAL" ]; then
    mv -f "$REAL" "$LIVE"
    echo "restored -- the game is back to its own libSceJobManager.dll"
  else
    echo "nothing to restore"
  fi
  rm -f "$INI" "$GAME/$MARKER"
  exit 0
  ;;
--install) ;;
*) usage ;;
esac

if is_ours "$LIVE" && [ -f "$REAL" ] && cmp -s "$LIVE" "$PROXY" && [ -f "$INI" ]; then
  echo "the fix is already installed, nothing to do"
  exit 0
fi
if is_ours "$LIVE" && [ ! -f "$REAL" ]; then
  echo "error: $LIVE is already the fix but $REAL is gone." >&2
  echo "       Verify the game files in Steam, then run this again." >&2
  exit 1
fi
# The saved original must never be overwritten (see install-sm2-fix.sh).
if ! is_ours "$LIVE" && [ -f "$REAL" ]; then
  echo "error: $REAL already exists and $LIVE is not this fix." >&2
  echo "       Run this with --restore first, or verify the game files in Steam." >&2
  exit 1
fi

if ! is_ours "$LIVE"; then
  echo "[1/3] checking the proxy forwards everything the game imports"
  if ! live_exports="$(/usr/bin/perl "$EXPORTS" exports "$LIVE" 2>&1)"; then
    echo "error: cannot read the exports of $LIVE" >&2
    exit 1
  fi
  proxy_exports="$(/usr/bin/perl "$EXPORTS" exports "$PROXY")"
  missing="$(comm -23 <(printf '%s\n' "$live_exports" | sort) <(printf '%s\n' "$proxy_exports" | sort))"
  if [ -n "$missing" ]; then
    echo "error: this build's libSceJobManager exports symbols the shipped proxy does not:" >&2
    echo "$missing" | sed 's/^/       /' >&2
    exit 1
  fi
else
  echo "[1/3] the fix's libSceJobManager.dll is in place"
fi

echo "[2/3] settings"
if [ -f "$INI" ]; then
  echo "  keeping $(basename "$INI"): $(sed -n 's/^equal_depth[[:space:]]*=[[:space:]]*//p' "$INI" | head -1)"
else
  printf '# God of War depth fix (MacGameVideoFix): what an EQUAL depth test becomes.\n# greater_equal (the fix), less_equal, or off.\nequal_depth = greater_equal\n' > "$INI.new"
  mv -f "$INI.new" "$INI"
  echo "  equal_depth = greater_equal"
fi

echo "[3/3] installing"
if is_ours "$LIVE"; then
  if ! cmp -s "$LIVE" "$PROXY"; then
    # The original stays under $REAL; only our own file is replaced.
    cp "$PROXY" "$LIVE.new" && mv -f "$LIVE.new" "$LIVE" || {
      rm -f "$LIVE.new"; echo "error: could not replace the older fix DLL" >&2; exit 1; }
  fi
else
  # The original is moved aside before anything is written over it
  # (see install-p5s-bridge.sh for the failure this order prevents).
  mv -f "$LIVE" "$REAL" || { echo "error: could not move the original aside; nothing was changed" >&2; exit 1; }
  [ -f "$REAL" ] || { echo "error: the original is not where it should be; refusing to write" >&2; exit 1; }
  cp "$PROXY" "$LIVE" || {
    echo "error: could not install the fix; putting the original back" >&2
    mv -f "$REAL" "$LIVE" || echo "       and that failed too -- it is at $REAL" >&2
    exit 1
  }
fi
echo
echo "installed"
echo "  EQUAL depth tests are loosened; the game writes $MARKER beside itself"
