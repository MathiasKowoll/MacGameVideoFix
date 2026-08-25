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
# TOOLKIT. This title runs on Apple's Game Porting Toolkit 3.0 and stalls on
# 4.0b2, which is what CrossOver Preview ships by default. That is a defect in
# the newer toolkit rather than anything a proxy can reach -- see the wiki --
# and the app can put 3.0 back.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROXY="${PROXY_DLL:-$HERE/dstorage-ng4.dll}"

usage() { sed -n '3,20p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="${1%/}"
ACTION="${2:-install}"
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
  if is_ours "$CARRIER" && [ -f "$REAL" ]; then echo installed
  elif is_ours "$CARRIER"; then echo broken
  elif [ ! -f "$CARRIER" ] && [ -f "$REAL" ]; then echo half
  else echo absent; fi

  # Said either way, because both are needed and neither is this script's doing.
  if [ -f "$GAME/dstoragecore.dll" ]; then
    echo "warning: dstoragecore.dll is still in place; the game needs it renamed" >&2
  fi
}

case "$ACTION" in
  --status) status; exit 0 ;;
  --restore)
      # Put the original back only if ours is the one in the way. Restoring over
      # a folder that was never patched would delete a real DLL.
      [ -f "$REAL" ] || { echo "nothing to restore"; exit 0; }
      rm -f "$CARRIER"
      mv "$REAL" "$CARRIER"
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
if [ ! -f "$REAL" ]; then
  mv "$CARRIER" "$REAL"
fi
cp "$PROXY" "$CARRIER"
echo "installed — answer the codec gate, and decode in software"
