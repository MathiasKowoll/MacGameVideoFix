#!/usr/bin/env bash
#
# Install (or remove) the startup fix for METAL GEAR SOLID 4.
#
# Nothing the game ships is edited. One DLL is renamed and one is added:
#
#   bink2w64.dll       <- our proxy, forwards all 76 symbols to bink2w64_real.dll
#   bink2w64_real.dll  <- RAD's original, untouched
#
#   install-mgs4-fix.sh <game folder>            install
#   install-mgs4-fix.sh <game folder> --restore  remove
#   install-mgs4-fix.sh <game folder> --status   report what is in place
#
# <game folder> is either the one holding the MGS4 folder, e.g.
#   .../steamapps/common/METAL GEAR SOLID 4
# or the MGS4 folder itself. Both work.
#
# WHAT THIS FIXES, and what it does not.
#
# Not video. This title decodes with Bink, which carries its own decoder and
# never touches Media Foundation, GStreamer or D3D video -- measured: it imports
# none of them, and in a full run it opens no .bk2 before the fault. It plays on
# stock CrossOver with nothing installed.
#
# What it does is load its audio banks behind a busy-wait. Measured on an M4 Max:
# four and a half million Sleep calls before the menu, about fifteen thousand a
# second, with all file I/O already complete and nothing advancing. On Windows
# that costs little; under Wine each crossing costs more, and twenty threads
# doing it at once starve whichever one holds the work. The title sits on a black
# screen -- rendering, at fifty-six frames a second, with nothing to draw -- until
# Steam's client pipe times out and it kills itself with a fatal assert, which is
# what a person sees as a crash with a click of audio.
#
# The proxy hooks Sleep and turns every sixty-fourth Sleep(0) into a Sleep(1),
# which yields the processor instead of spinning against itself. First run with
# it in place: 36,160 spins, 565 of them yielded -- a hundred and twenty-five
# times fewer than without. The yields do not pace the game, they let it finish.
#
# The divisor is not compiled in. Put a number in C:\mgvf-sleep.txt inside the
# bottle to change it -- smaller yields more often -- and 0 there disables the
# fix without uninstalling it, which is the honest way to check whether it is
# still doing anything on a machine that is not this one.
#
# Loading is still slow: three to four minutes of banks before the menu, and the
# window is black for most of it. That is the title, not this fix. Give it time
# before deciding it has hung.
#
# MGVF-GAME: METAL GEAR SOLID 4 | mgs4.exe | MGS4
# MGVF-WHY: Black window and a click of audio, then it exits. Nothing to do with video: it loads its audio banks behind a busy-wait that starves itself under Wine, and Steam's pipe times out first. It plays on stock CrossOver untouched.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROXY="${PROXY_DLL:-$HERE/bink2w64-mgs4.dll}"

usage() { sed -n '3,20p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="${1%/}"
ACTION="${2:-install}"
[ -d "$GAME" ] || { echo "error: no such folder: $GAME" >&2; exit 1; }

# The carrier lives one level down, beside mgs4.exe. Accept either the folder
# Steam made or the one the executable is in, because both are the obvious thing
# to pick and getting it wrong should not be a silent no-op.
if [ -f "$GAME/MGS4/bink2w64.dll" ] || [ -f "$GAME/MGS4/bink2w64_real.dll" ]; then
  DIR="$GAME/MGS4"
elif [ -f "$GAME/bink2w64.dll" ] || [ -f "$GAME/bink2w64_real.dll" ]; then
  DIR="$GAME"
else
  echo "error: no bink2w64.dll under $GAME" >&2
  echo "       Pick the folder METAL GEAR SOLID 4 is installed in." >&2
  exit 1
fi

CARRIER="$DIR/bink2w64.dll"
REAL="$DIR/bink2w64_real.dll"

# Ours names what it forwards to in its import table, so the two are told apart
# by what they reference rather than by which sits in the slot.
#
# The string to look for is the forwarder prefix, "bink2w64_real." followed by a
# symbol -- not "bink2w64_real.dll", which is what the import directory calls the
# library and which does not appear in the file. Written the wrong way first, and
# --status answered "absent" over a working install.
is_ours() {
  [ -f "$1" ] || return 1
  LC_ALL=C grep -qa "bink2w64_real\.Bink" "$1"
}

# One word, and one of exactly four: the app matches the word, not the line.
status() {
  if is_ours "$CARRIER" && [ -f "$REAL" ]; then echo installed
  elif is_ours "$CARRIER"; then echo broken
  elif [ ! -f "$CARRIER" ] && [ -f "$REAL" ]; then echo half
  else echo absent; fi
}

case "$ACTION" in
  --status) status; exit 0 ;;
  --restore)
      # Only undo our own work. Restoring over a folder that was never patched
      # would delete a real DLL.
      [ -f "$REAL" ] || { echo "nothing to restore"; exit 0; }
      if [ -f "$CARRIER" ] && ! is_ours "$CARRIER"; then
        echo "error: $CARRIER is RAD's own, not ours -- nothing was changed." >&2
        echo "       $REAL is a leftover copy; remove it by hand if you want it gone." >&2
        exit 1
      fi
      rm -f "$CARRIER"
      mv "$REAL" "$CARRIER"
      echo "restored — the game is back to its own bink2w64.dll"
      exit 0 ;;
  install) ;;
  *) usage ;;
esac

[ -f "$PROXY" ] || { echo "error: no proxy at $PROXY" >&2; exit 1; }

# Idempotent, and safe after a Steam verification: the question is which of the
# two files is ours, not whether $REAL happens to exist.
moved=0
if is_ours "$CARRIER"; then
  [ -f "$REAL" ] || {
    echo "error: $CARRIER is already ours but $REAL is gone." >&2
    echo "       Verify the game files in Steam, then run this again." >&2
    exit 1
  }
else
  [ -f "$CARRIER" ] || { echo "error: no bink2w64.dll in $DIR" >&2; exit 1; }
  [ -e "$REAL" ] && echo "  $REAL was left over from before; the live bink2w64.dll is RAD's, so it replaces it"
  mv -f "$CARRIER" "$REAL"
  moved=1
fi

cp "$PROXY" "$CARRIER" || {
  [ "$moved" = 1 ] && mv -f "$REAL" "$CARRIER" 2>/dev/null
  echo "error: could not install the proxy" >&2; exit 1
}

echo "installed — the spin loop gets a real yield; loading is still slow"
