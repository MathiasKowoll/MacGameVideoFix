#!/usr/bin/env bash
#
# Install the CONTROL Resonant window fix.
#
#     install-ctl-fix.sh <game folder>            install
#     install-ctl-fix.sh <game folder> --status   report
#     install-ctl-fix.sh <game folder> --restore  undo
#
# The folder is the one Steam installed, holding CONTROLResonant.exe.
#
# WHAT IT FIXES. On D3DMetal the lit rooms behind the city's windows come out
# as flat, saturated colours -- violet, cyan, yellow -- that spread to
# neighbouring windows as the camera moves. One pixel shader draws them,
# interiorPS. D3DMetal runs its per-window texture choice as if it were the
# same for every pixel of a group, and something outside the shaders, most
# likely texture streaming, breaks it again after a minute even when that is
# worked around. diagnostics/ctl-window-shaders.md holds the eleven rounds that
# found it and the rebuild that proved the first fault.
#
# So the fix hides that one shader. The windows show dark glass instead of
# rooms; nothing else in the frame changes, and the game plays as before. A
# game update that changes the shader makes the fix match nothing and do
# nothing, which is safe.
#
# It is not the only setting this title needs on D3DMetal 4.0b2: "Metal 4
# backend" OFF (faces and hair), and ray tracing off -- the game crashes with
# any of it, and rewrites its own path-tracing setting, so RaccoonBot's
# "Ray tracing (DXR)" OFF is the reliable way.
#
# THE CARRIER IS THE GAME'S OWN. PxFoundation_x64.dll is part of PhysX, the
# game imports it statically, and its exports are forwarded straight back to
# the renamed original. Nothing is redistributed, no registry key is written
# and no CrossOver file is copied.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# MGVF-SCOPE: folder
# MGVF-GAME: CONTROL Resonant | CONTROLResonant.exe |
# MGVF-WHY: Hides the window-interior shader D3DMetal draws as flat colours.

set -euo pipefail

usage() { sed -n '3,37p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

BIN="$GAME"
LIVE="$BIN/PxFoundation_x64.dll"
REAL="$BIN/PxFoundation_x64_real.dll"
PROXY="$HERE/PxFoundation_x64-ctl.dll"
EXPORTS="$HERE/pe.pl"
MARKER='ctl-window-fix.log'

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

# The same question asked a way that cannot go stale: our proxy forwards every
# export to PxFoundation_x64_real, so the EXPORT table carries one
# "PxFoundation_x64_real.<symbol>" string per forwarder. A genuine library never
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
  LC_ALL=C grep -qaF "PxFoundation_x64_real." "$1"
}

[ -f "$GAME/CONTROLResonant.exe" ] || {
  echo "error: no 'CONTROLResonant.exe' in $GAME" >&2
  echo "       Pick the folder CONTROL Resonant is installed in." >&2
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
    echo "restored — the game is back to its own PxFoundation"
  elif looks_like_ours "$LIVE"; then
    # Our proxy is here and the game's own DLL is not saved anywhere. There is
    # nothing to put back, and saying "nothing of ours is installed" about a
    # file that is plainly ours sends the reader looking in the wrong place.
    echo "our proxy is installed, but the game's own PxFoundation_x64.dll is not saved here." >&2
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

echo "[1/3] checking PxFoundation_x64.dll"
[ -f "$LIVE" ] || { echo "error: no PxFoundation_x64.dll in $BIN" >&2; exit 1; }
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
  echo "error: this build's PxFoundation exports symbols the shipped proxy does not:" >&2
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
echo "  the game's own PxFoundation_x64.dll is now PxFoundation_x64_real.dll"
echo "  and every one of its exports reaches it"
