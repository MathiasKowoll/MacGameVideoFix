#!/usr/bin/env bash
#
# Install (or remove) the startup fix for RESONANCE: A PLAGUE TALE LEGACY.
#
# Nothing the game ships is edited. One DLL is renamed and one is added:
#
#   NvCloth_x64.dll       <- our proxy, forwards all 42 symbols to the original
#   NvCloth_x64_real.dll  <- the game's own, untouched
#
#   install-resonance-fix.sh <game folder>            install
#   install-resonance-fix.sh <game folder> --restore  remove
#   install-resonance-fix.sh <game folder> --status   report what is in place
#
# <game folder> is the one holding Resonance.exe, e.g.
#   .../steamapps/common/Resonance A Plague Tale Legacy
#
# WHAT THIS FIXES.
#
# The title refuses to start where the device reports Shader Model 6.6:
#
#     Fatal error
#     Shader Model 6.7 is not supported by this device!
#     Maximum Shader Model supported is 6.6
#
# One comparison, cmp eax, 67h, and everything either side of it is correct --
# D3DMetal has 6.6 because it has 6.6, and the game's own store page asks for
# 6.6. The byte becomes 66h in memory and the check passes. Nothing on disk is
# touched, so a Steam verification does not undo it and an update does not fight
# it. Found by pattern, not by address: the offsets that circulate are per-build
# and were already wrong for the copy this was written against.
#
# What it costs is known: some puzzle textures and effects are missing, because
# the title does have 6.7 paths and this keeps it off them. The rest plays.
#
# AND ONE THING THIS CANNOT FIX, which you have to do yourself.
#
# THE DISPLAY MUST BE 16:9. Measured: on a 3456x2234 panel -- 1.547:1, which is
# every Apple laptop -- the game renders four hundred draws a frame at eighty
# frames a second into a window that stays black. Menu, cutscenes, everything.
# It filters the display mode list for 16:9, finds nothing, and composes into a
# region that is never shown. At 1920x1080 it plays.
#
# So set the display to a 16:9 mode before launching -- with the game's own
# graphics options, a tool like BetterDisplay, or a CrossOver virtual desktop.
# This is why the same title runs for people on 16:9 monitors with the same
# D3DMetal and no fix at all beyond the byte.
#
# AND THE VIDEOS, which are not a video problem.
#
# The logo and tutorial MP4s never appear, at any resolution, and it is worth
# saying plainly that nothing in this project can fix that. They are H.264 High
# in MP4 with AAC -- the most ordinary file there is -- and the whole path works:
#
#     MediaEngineClassFactory::CreateInstance -> 0x00000000
#     OnVideoStreamTick: asked 1200 times, a frame was ready 428 of them
#     TransferVideoFrame -> ok (360 so far)      0 failures
#
# The title plays them through IMFMediaEngine, created over COM -- which is why
# nothing here saw them for an afternoon: it never calls MFTEnumEx and never
# creates a source reader, so the five entry points this probe swapped were the
# wrong five. Underneath, mfmp4srcsnk demuxes and winegstreamer decodes, both
# measured present and both working.
#
# Then the frames are handed to a texture of the game's own and never drawn.
# Confirmed by filling the surrounds of every transferred frame with opaque
# magenta: three hundred and sixty frames delivered, forty percent of the
# surface painted a colour the game does not contain, and the screen stayed
# black. That surface does not reach the screen, and what happens to it after
# TransferVideoFrame is inside the engine, not inside anything we can hook.
#
# Ruled out along the way, so nobody repeats it: MetalFX and its temporal
# scaling, the Metal 4 backend, the video memory budget (the adapter reports 76
# GB; the zero in the game's own crash report is its own number), D3D11 against
# D3D12, Game Porting Toolkit 3.0 against 4.0b2, and patching the executable on
# disk exactly as the community fix says -- which is equivalent to this and just
# as black on a non-16:9 display.
#
# MGVF-SCOPE: folder
# MGVF-GAME: RESONANCE: A PLAGUE TALE LEGACY | Resonance.exe |
# MGVF-WHY: Refuses to start: "Shader Model 6.7 is not supported by this device". Needs a 16:9 display as well -- on an Apple laptop panel it renders into a window that stays black.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROXY="${PROXY_DLL:-$HERE/NvCloth_x64-resonance.dll}"

usage() { sed -n '3,20p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="${1%/}"
ACTION="${2:-install}"
# A read-only caller sets MGVF_STATUS_ONLY=1. The default above is the
# DESTRUCTIVE branch, so without this the read-only property of a survey rests
# on the literal --status never being lost from one line of one caller.
# Structural beats positional.
#
# Four installers were missing this while the other nine had it, so a launcher
# setting the variable got a read-only guarantee that silently did not cover
# them. Found by the RaccoonBot session, which set the variable and then went
# and checked which scripts actually read it rather than trusting that they did.
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then ACTION=--status; fi

[ -d "$GAME" ] || { echo "error: no such folder: $GAME" >&2; exit 1; }

CARRIER="$GAME/NvCloth_x64.dll"
REAL="$GAME/NvCloth_x64_real.dll"

[ -f "$GAME/Resonance.exe" ] || {
  echo "error: no Resonance.exe in $GAME" >&2
  echo "       Pick the folder RESONANCE is installed in." >&2
  exit 1
}

# Ours names what it forwards to, so the two are told apart by what they
# reference rather than by which one sits in the slot.
is_ours() {
  [ -f "$1" ] || return 1
  LC_ALL=C grep -qa "NvCloth_x64_real\." "$1"
}

status() {
  if is_ours "$CARRIER" && [ -f "$REAL" ]; then echo installed
  elif is_ours "$CARRIER"; then echo broken
  elif [ ! -f "$CARRIER" ] && [ -f "$REAL" ]; then echo half
  else echo absent; fi
  echo "note: this fix gets the game to start; it still needs a 16:9 display to show anything" >&2
}

case "$ACTION" in
  --status) status; exit 0 ;;
  --restore)
      [ -f "$REAL" ] || { echo "nothing to restore"; exit 0; }
      if [ -f "$CARRIER" ] && ! is_ours "$CARRIER"; then
        echo "error: $CARRIER is the game's own, not ours -- nothing was changed." >&2
        echo "       $REAL is a leftover copy; remove it by hand if you want it gone." >&2
        exit 1
      fi
      rm -f "$CARRIER"
      mv "$REAL" "$CARRIER"
      echo "restored — the game is back to its own NvCloth_x64.dll"
      exit 0 ;;
  install) ;;
  *) usage ;;
esac

[ -f "$PROXY" ] || { echo "error: no proxy at $PROXY" >&2; exit 1; }

moved=0
if is_ours "$CARRIER"; then
  [ -f "$REAL" ] || {
    echo "error: $CARRIER is already ours but $REAL is gone." >&2
    echo "       Verify the game files in Steam, then run this again." >&2
    exit 1
  }
else
  [ -f "$CARRIER" ] || { echo "error: no NvCloth_x64.dll in $GAME" >&2; exit 1; }
  [ -e "$REAL" ] && echo "  $REAL was left over from before; the live NvCloth_x64.dll is the game's own, so it replaces it"
  mv -f "$CARRIER" "$REAL"
  moved=1
fi

cp "$PROXY" "$CARRIER" || {
  [ "$moved" = 1 ] && mv -f "$REAL" "$CARRIER" 2>/dev/null
  echo "error: could not install the proxy" >&2; exit 1
}

echo "installed — the game will start"
echo "  set the display to 16:9 before launching, or nothing will be drawn" >&2
