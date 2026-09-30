#!/usr/bin/env bash
#
# Install The Witcher 3 Remastered black-screen fix.
#
#     install-w3-fix.sh <game folder>            install
#     install-w3-fix.sh <game folder> --status   report
#     install-w3-fix.sh <game folder> --restore  undo
#
# The folder is the one Steam or GOG installed, holding bin/x64_dx12.
#
# WHAT IT FIXES. Since patch 5.00b the game hangs on a black window at full
# CPU. At startup it builds one stream-output pipeline -- a geometry shader and
# no pixel shader -- and compiling it crashes Apple's shader converter inside
# D3DMetal; the game's crash handler then deadlocks. The proxy refuses that
# pipeline before it reaches D3DMetal, and the game carries on without it.
# Diagnosis: github.com/tholtman1-del/witcher3-crossover-fix (MIT).
#
# The same proxy lets the game offer DLSS. The game turns NVIDIA Streamline
# off when it finds Wine, so the menu said DLSS was not supported; the one
# check that decides it is told there is no Wine, and D3DMetal then carries
# DLSS to MetalFX. Frame generation still does not work.
#
# It also gives the launch a bin/x64. 5.00b ships only bin/x64_dx12, and a
# launch that still asks for bin/x64/witcher3.exe fails with "path not found".
# When bin/x64 is missing, a link to x64_dx12 is made; a folder already there
# (a copy made by hand) is left alone and reported, because a copy goes stale
# with the next game update and a link cannot.
#
# THE CARRIER IS THE GAME'S OWN. amd_fidelityfx_loader_dx12.dll is AMD's
# FidelityFX loader, the game imports it statically, and its five exports are
# forwarded straight back to the renamed original. Nothing is redistributed,
# no registry key is written and no CrossOver file is copied.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# MGVF-SCOPE: folder
# MGVF-GAME: The Witcher 3: Wild Hunt | witcher3.exe | bin/x64_dx12
# MGVF-WHY: Refuses the stream-output pipeline that crashes D3DMetal's shader converter, and lets the game offer DLSS.

set -euo pipefail

usage() { sed -n '3,32p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

BIN="$GAME/bin/x64_dx12"
X64="$GAME/bin/x64"
LIVE="$BIN/amd_fidelityfx_loader_dx12.dll"
REAL="$BIN/amd_fidelityfx_loader_dx12_real.dll"
PROXY="$HERE/amd_fidelityfx_loader_dx12-w3.dll"
EXPORTS="$HERE/pe.pl"
MARKER='w3-streamout-fix.log'

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

# The same question asked a way that cannot go stale: our proxy forwards every
# export to amd_fidelityfx_loader_dx12_real, so the EXPORT table carries one
# "amd_fidelityfx_loader_dx12_real.<symbol>" string per forwarder. A genuine library never
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
  LC_ALL=C grep -qaF "amd_fidelityfx_loader_dx12_real." "$1"
}

# bin/x64 as this script left it: a link to x64_dx12 and nothing else.
x64_is_ours() { [ -L "$X64" ] && [ "$(readlink "$X64")" = "x64_dx12" ]; }

x64_state() {
  if x64_is_ours; then echo "bin/x64: linked to x64_dx12"
  elif [ -L "$X64" ]; then echo "bin/x64: a link to $(readlink "$X64"), not ours"
  elif [ -d "$X64" ]; then echo "bin/x64: a folder, left as it is (a copy goes stale with the next update)"
  else echo "bin/x64: missing"; fi
}

[ -f "$BIN/witcher3.exe" ] || {
  echo "error: no 'bin/x64_dx12/witcher3.exe' in $GAME" >&2
  echo "       Pick the folder The Witcher 3 is installed in." >&2
  exit 1
}

case "$MODE" in
--status)
  if is_ours "$LIVE" && [ -f "$REAL" ]; then echo installed
  elif is_ours "$LIVE"; then echo broken
  elif [ ! -f "$LIVE" ] && [ -f "$REAL" ]; then echo half
  else echo absent; fi
  x64_state
  exit 0
  ;;
--restore)
  if x64_is_ours; then rm "$X64"; echo "removed the bin/x64 link"; fi
  # mv -f overwrites, so the original goes back in one step. Removing $LIVE
  # first would open a window where neither file is in place, and being
  # interrupted inside it left a state that neither --restore nor --install
  # would touch afterwards.
  if [ -f "$REAL" ] && ! looks_like_ours "$REAL" \
       && { looks_like_ours "$LIVE" || [ ! -e "$LIVE" ]; }; then
    mv -f "$REAL" "$LIVE"
    echo "restored — the game is back to its own FidelityFX loader"
  elif looks_like_ours "$LIVE"; then
    # Our proxy is here and the game's own DLL is not saved anywhere. There is
    # nothing to put back, and saying "nothing of ours is installed" about a
    # file that is plainly ours sends the reader looking in the wrong place.
    echo "our proxy is installed, but the game's own amd_fidelityfx_loader_dx12.dll is not saved here." >&2
    echo "  Verify the game files in Steam or GOG to get it back, then run --install." >&2
    exit 1
  else
    echo "nothing of ours is installed"
  fi
  exit 0
  ;;
--install) ;;
*) usage ;;
esac

# A launch that still asks for bin/x64/witcher3.exe finds the DX12 build. The
# link is relative, so it survives the library moving to another drive.
if [ ! -e "$X64" ] && [ ! -L "$X64" ]; then
  if ln -s x64_dx12 "$X64" 2>/dev/null; then
    echo "linked bin/x64 to x64_dx12"
  else
    echo "note: this drive cannot hold a link, so bin/x64 is still missing." >&2
    echo "      A launch that asks for bin/x64 will fail with \"path not found\"." >&2
  fi
fi

if is_ours "$LIVE" && [ -f "$REAL" ]; then
  echo "the fix is already installed, nothing to do"
  x64_state
  exit 0
fi

echo "[1/3] checking amd_fidelityfx_loader_dx12.dll"
[ -f "$LIVE" ] || { echo "error: no amd_fidelityfx_loader_dx12.dll in $BIN" >&2; exit 1; }
if is_ours "$LIVE"; then
  echo "error: $LIVE is already a proxy but $REAL is gone." >&2
  echo "       Verify the game files in Steam or GOG, then run this again." >&2
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
  echo "error: this build's FidelityFX loader exports symbols the shipped proxy does not:" >&2
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
echo "  the game's own amd_fidelityfx_loader_dx12.dll is now amd_fidelityfx_loader_dx12_real.dll"
echo "  and every one of its exports reaches it"
x64_state
