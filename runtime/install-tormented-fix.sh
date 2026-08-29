#!/usr/bin/env bash
#
# Install the TORMENTED SOULS 2 startup fix.
#
#     install-tormented-fix.sh <game folder>            install
#     install-tormented-fix.sh <game folder> --status   report
#     install-tormented-fix.sh <game folder> --restore  undo
#
# The folder is the one Steam installed, holding TormentedSouls2.exe.
#
# WHAT IT FIXES. The game shows a window and dies with
# EXCEPTION_ACCESS_VIOLATION reading 0xfffffffffffffff8, before its first
# frame. Nothing in the graphics stack refuses it anything -- the renderer
# builds 3736 buffers, 1571 textures and 37 compute shaders without a single
# failure, and the display and its modes enumerate correctly.
#
# The fault is the game's own. It walks the resolutions it is offered, computes
# the aspect ratio of each, and keeps only those strictly between 1.76 and 1.79
# -- 16:9 and nothing else. There is no branch after that comparison for the
# case where nothing matches.
#
# A MacBook display is not 16:9. On a 2056x1329 screen every mode on offer is
# 1.6 or 1.547, none passes, the list stays empty, the search for a current
# mode returns INDEX_NONE, and the game indexes the empty list with -1.
#
# So this puts 16:9 modes on the list: the whole 16:9 ladder that fits inside
# the desktop, so the resolution menu has real choices rather than one forced
# entry. It costs the game nothing -- it renders into a swap chain of whatever
# size it asks for either way -- and gives its filter something to keep. When a
# 16:9 mode is already there, nothing is added, which is every 16:9 display.
#
# Two more guards ride along and are inert here: one answers E_INVALIDARG for a
# shader container with no root signature in it, and one obtains a compute
# device at feature level 11_0 when 1_0_CORE is refused. The second does fire
# on this game, twice per launch, though it was not what stopped it starting.
#
# THE CARRIER IS THE GAME'S OWN. OpenColorIO_2_3.dll is colour management, the
# game imports it statically, and its 1008 exports are forwarded straight back
# to the renamed original. Nothing is redistributed, no registry key is written
# and no CrossOver file is copied.
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
# MGVF-GAME: Tormented Souls 2 | TormentedSouls2.exe | TormentedSouls2/Binaries/Win64
# MGVF-WHY: The same D3D12 guards on the colour-management library the game imports.

set -euo pipefail

usage() { sed -n '3,41p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

BIN="$GAME/TormentedSouls2/Binaries/Win64"
LIVE="$BIN/OpenColorIO_2_3.dll"
REAL="$BIN/OpenColorIO_2_3_real.dll"
PROXY="$HERE/OpenColorIO_2_3-tormented.dll"
EXPORTS="$HERE/pe.pl"
MARKER='d3d12-guards.log'

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

# The same question asked a way that cannot go stale: our proxy forwards every
# export to OpenColorIO_2_3_real, so the EXPORT table carries one
# "OpenColorIO_2_3_real.<symbol>" string per forwarder. A genuine library never
# forwards to its own _real variant. Markers are for reporting; this is what the
# destructive step is allowed to rely on.
#
# The pattern is the prefix with its dot and NOT the ".dll" spelling, which
# never appears in the file: build-proxy.sh emits pure forwarders and no import
# descriptor, so the old test could not match, ever, and degenerated into the
# marker check it was written to outlive.
looks_like_ours() {
  [ -f "$1" ] || return 1
  is_ours "$1" && return 0
  LC_ALL=C grep -qaF "OpenColorIO_2_3_real." "$1"
}

[ -f "$GAME/TormentedSouls2.exe" ] || {
  echo "error: no 'TormentedSouls2.exe' in $GAME" >&2
  echo "       Pick the folder Tormented Souls 2 is installed in." >&2
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
  # mv -f overwrites, so the original goes back in one step. Removing $LIVE
  # first would open a window where neither file is in place, and being
  # interrupted inside it left a state that neither --restore nor --install
  # would touch afterwards.
  if [ -f "$REAL" ] && ! looks_like_ours "$REAL" \
       && { looks_like_ours "$LIVE" || [ ! -e "$LIVE" ]; }; then
    mv -f "$REAL" "$LIVE"
    echo "restored — the game is back to its own OpenColorIO"
  elif looks_like_ours "$LIVE"; then
    # Our proxy is here and the game's own DLL is not saved anywhere. There is
    # nothing to put back, and saying "nothing of ours is installed" about a
    # file that is plainly ours sends the reader looking in the wrong place.
    echo "our proxy is installed, but the game's own OpenColorIO_2_3.dll is not saved here." >&2
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

echo "[1/3] checking OpenColorIO_2_3.dll"
[ -f "$LIVE" ] || { echo "error: no OpenColorIO_2_3.dll in $BIN" >&2; exit 1; }
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
  echo "error: this build's OpenColorIO exports symbols the shipped proxy does not:" >&2
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
echo "  the game's own OpenColorIO_2_3.dll is now OpenColorIO_2_3_real.dll"
echo "  and every one of its exports reaches it"
