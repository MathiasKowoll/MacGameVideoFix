#!/usr/bin/env bash
#
# Install (or remove) the fix for NINJA GAIDEN 4.
#
# Nothing the game ships is edited. One DLL is renamed and one is added:
#
#   dstorage.dll       <- our proxy, forwards every symbol to dstorage_real.dll
#   dstorage_real.dll  <- the game's original, untouched
#
# dstorage.dll is Microsoft's DirectStorage, which this title imports directly
# and which has nothing to do with video -- so a proxy in front of it cannot
# disturb what it does. It is also loaded early, which this fix needs: the gate
# it answers is asked before the game opens anything.
#
#   install-ng4-fix.sh <game folder>            install
#   install-ng4-fix.sh <game folder> --restore  remove
#   install-ng4-fix.sh <game folder> --status   report what is in place
#
# <game folder> is the one holding NINJAGAIDEN4-Steam.exe, e.g.
#   .../steamapps/common/NINJAGAIDEN4
#
# WHAT THIS FIXES, and what it does not.
#
# The game asks Media Foundation for a VP9 decoder and counts what comes back.
# Zero is fatal and immediate -- "Windows is missing required components… The
# game will now exit" -- so the count is answered. It then refuses the DXGI
# device manager, which sends decoding to software and keeps frames in system
# memory; leaving it alone reaches the video and dies inside Metal.
#
# Neither of those makes a video play on its own. The container does: CrossOver
# ships no Matroska demuxer, and 399 of this title's 400 `.msd` files are plain
# WebM. Stage the codec from the app -- or runtime/stage-codecs.sh -- or this
# fix will get the game to its menu with the cutscene still missing.
#
# DirectStorage must also be off: rename dstoragecore.dll beside the game. The
# title takes another I/O path and plays; with it in place it dies earlier, for
# reasons that are its own and not this project's.
#
# TOOLKIT. This title needs Apple's Game Porting Toolkit 3.0. On 4.0b2, measured
# 2026-08-31, the Metal 4 submission queue parks in IOGPUCommandQueueWaitMTLEvent
# with the GPU idle, so nothing is ever presented and the window does not reach
# full screen; it also takes about five times as long to reach a cutscene. Setting
# D3DM_MTL4=0 removes that queue and the stall with it. Neither is anything a
# proxy can reach -- see the wiki -- and the app can put 3.0 back.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
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
# MGVF-GAME: NINJA GAIDEN 4 | NINJAGAIDEN4-Steam.exe | 
# MGVF-WHY: The game asks Media Foundation whether anything decodes VP9 and quits if the answer is zero. Needs the staged Matroska demuxer too.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROXY="${PROXY_DLL:-$HERE/dstorage-ng4.dll}"

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

CARRIER="$GAME/dstorage.dll"
REAL="$GAME/dstorage_real.dll"

# Our proxy names the file it forwards to in its import table, so a genuine
# DirectStorage and ours are told apart by what they reference rather than by
# which one happens to sit in the slot.
is_ours() {
  [ -f "$1" ] || return 1
  LC_ALL=C grep -qa "dstorage_real.dll" "$1"
}

# One word, and one of exactly four. The app matches the word rather than the
# first line -- an installer that answers "installed: ..." falls through to "not
# applied" and the control that would put the DLL back is greyed out. Advisories
# go to stderr for the same reason: the app merges the streams.
status() {
  # DirectStorage decides the word, it does not merely annotate it. The advisory
  # below goes to stderr, the app merges the streams and then keeps only lines
  # that are one of the four words -- so a warning about a game that is going to
  # die anyway reached nobody, under a green `installed`. If the carrier is ours
  # but DirectStorage is still live, the fix is in place and not working, which
  # is what `broken` means.
  if is_ours "$CARRIER" && [ -f "$REAL" ]; then
    if [ -f "$GAME/dstoragecore.dll" ]; then echo broken; else echo installed; fi
  elif is_ours "$CARRIER"; then echo broken
  elif [ ! -f "$CARRIER" ] && [ -f "$REAL" ]; then echo half
  else echo absent; fi

  # Said either way, because both are needed.
  if [ -f "$GAME/dstoragecore.dll" ]; then
    echo "warning: dstoragecore.dll is still in place; the game needs it renamed" >&2
  fi
}

case "$ACTION" in
  --status) status; exit 0 ;;
  --restore)
      # Put the original back only if ours is the one in the way. Restoring over
      # a folder that was never patched would delete a real DLL.
      #
      # That was a comment and not a check. The only guard was `[ -f "$REAL" ]`,
      # so a folder whose live dstorage.dll is the game's own -- after a Steam
      # verification, say -- had that genuine DLL removed and replaced by
      # whatever the stale copy happened to be.
      [ -f "$REAL" ] || { echo "nothing to restore"; exit 0; }
      if [ -f "$CARRIER" ] && ! is_ours "$CARRIER"; then
        echo "error: $CARRIER is the game's own, not ours -- nothing was changed." >&2
        echo "       $REAL is a leftover copy; remove it by hand if you want it gone." >&2
        exit 1
      fi
      rm -f "$CARRIER"
      mv "$REAL" "$CARRIER"
      # Only undo the rename this script made, and never over a file that is
      # already there: a user who had renamed DirectStorage by hand keeps their
      # own arrangement.
      if [ -f "$GAME/dstoragecore.dll.mgvf-off" ]; then
        if [ -f "$GAME/dstoragecore.dll" ]; then
          # Both present: the game brought its own back, so the live one wins
          # and the copy is left alone rather than deleted. Install overwrites
          # it next time round, so this does not wedge anything.
          echo "note: $GAME/dstoragecore.dll.mgvf-off is a leftover copy; the game's own is back" >&2
        else
          mv "$GAME/dstoragecore.dll.mgvf-off" "$GAME/dstoragecore.dll"
        fi
      fi
      echo "restored — the game is back to its own dstorage.dll"
      exit 0 ;;
  install) ;;
  *) usage ;;
esac

[ -f "$PROXY" ] || { echo "error: no proxy at $PROXY" >&2; exit 1; }
[ -f "$CARRIER" ] || { echo "error: no dstorage.dll in $GAME" >&2; exit 1; }

# Idempotent: installing twice must not rename our own proxy into the slot the
# original belongs in, which would leave the game with two copies of the fix and
# no DirectStorage at all.
#
# Asking `[ ! -f "$REAL" ]` was the wrong question, and a destructive one. After
# a Steam verification the live dstorage.dll is the game's own again -- possibly
# newer -- while our saved copy is stale; that test saw $REAL and skipped the
# move, and the cp below then overwrote the freshly restored original, whose
# only copy it was. The right question is which of the two is ours.
moved=0
if is_ours "$CARRIER"; then
  [ -f "$REAL" ] || {
    echo "error: $CARRIER is already ours but $REAL is gone." >&2
    echo "       Verify the game files in Steam, then run this again." >&2
    exit 1
  }
else
  [ -e "$REAL" ] && echo "  $REAL was left over from before; the live dstorage.dll is the game's own, so it replaces it"
  mv -f "$CARRIER" "$REAL"
  moved=1
fi
# The rollback undoes this run, and only this run. Re-running over an install
# that is already ours moves nothing, so putting $REAL back there would not be a
# rollback -- it would be an uninstall performed by a failed repair.
cp "$PROXY" "$CARRIER" || {
  [ "$moved" = 1 ] && mv -f "$REAL" "$CARRIER" 2>/dev/null
  echo "error: could not install the proxy" >&2; exit 1
}

# DirectStorage off, which the header has always declared mandatory and which
# nothing here ever did. Reversible, and a no-op once it is already off.
#
# The move overwrites any leftover .mgvf-off on purpose. Refusing when one
# existed made the repair impossible exactly where it was needed: a Steam
# verification brings dstoragecore.dll back beside the old copy, --status calls
# that `broken`, and an install that declined to touch it left the state
# unreachable by any button while still printing "installed".
if [ -f "$GAME/dstoragecore.dll" ]; then
  mv -f "$GAME/dstoragecore.dll" "$GAME/dstoragecore.dll.mgvf-off"
  echo "  DirectStorage turned off: dstoragecore.dll -> dstoragecore.dll.mgvf-off"
fi
echo "installed — answer the codec gate, and decode in software"
