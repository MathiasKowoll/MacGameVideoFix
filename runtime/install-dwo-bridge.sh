#!/usr/bin/env bash
#
# Install (or remove) the video bridge for DYNASTY WARRIORS: ORIGINS.
#
# Nothing the game ships is edited. One DLL is renamed and one is added:
#
#   libxess.dll       <- our proxy, forwards every symbol to libxess_real.dll
#   libxess_real.dll  <- the game's original, untouched
#
# libxess is Intel's XeSS upscaler. It carries the fix because the game loads
# it directly and it has nothing to do with video, so a proxy in front of it
# cannot disturb anything it does.
#
#   install-dwo-bridge.sh <game folder>            install
#   install-dwo-bridge.sh <game folder> --restore  remove
#   install-dwo-bridge.sh <game folder> --status   report what is in place
#
# <game folder> is the one holding DWORIGINS.exe or WoLong.exe, e.g.
#   .../steamapps/common/DWORIGINS
#   .../steamapps/common/WoLongFallenDynasty
#
# Both titles ride libxess.dll and share this bridge: they decode video on a
# D3D11 device and present it with a D3D12 renderer, which is the shape this
# was written for.
#
# This presents frames, it does not decode them. DYNASTY WARRIORS needs a
# container the engine can open -- see the wiki -- and an earlier
# version of this file said it needed CrossOver patched with winevideo, which
# was withdrawn: both builds decode VP9 identically and what stable lacks is a
# WebM demuxer.
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
# MGVF-GAME: DYNASTY WARRIORS: ORIGINS | DWORIGINS.exe | 
# MGVF-GAME: Wo Long: Fallen Dynasty | WoLong.exe | 
# MGVF-WHY: The player needs D3D11 video interfaces D3DMetal lacks, and the decoded frame carried across to its D3D12 renderer.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROXY="${PROXY_DLL:-$HERE/libxess.dll}"
EXPORTS="$HERE/pe.pl"

# The proxy writes this path at runtime; finding it inside a DLL is how we tell
# our file apart from Intel's.
MARKER='dwo-video-bridge.log'

usage() { sed -n '3,26p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
# A read-only caller (scripts/support-bundle.sh) sets MGVF_STATUS_ONLY=1. The
# default above is the DESTRUCTIVE branch, so without this the read-only
# property of a whole support bundle rests on the literal --status never being
# lost from one line of one other script. Structural beats positional.
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi

# Plain if rather than [ -f ] && assignment: under set -e a failing test as the
# last command of the loop body would end the script here.
GAME_EXE=""
for e in DWORIGINS.exe WoLong.exe; do
  if [ -f "$GAME/$e" ]; then GAME_EXE="$e"; fi
done
[ -n "$GAME_EXE" ] || {
  echo "error: no DWORIGINS.exe or WoLong.exe in $GAME" >&2
  echo "       Point this at the folder holding the game's executable." >&2
  exit 1
}

LIVE="$GAME/libxess.dll"
REAL="$GAME/libxess_real.dll"

[ -f "$LIVE" ] || [ -f "$REAL" ] || {
  echo "error: no libxess.dll in $GAME" >&2
  echo "       This build of the game has no carrier for the bridge to ride on." >&2
  exit 1
}

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

status() {
  if is_ours "$LIVE" && [ -f "$REAL" ]; then echo installed
  elif is_ours "$LIVE"; then echo broken       # proxy present, original missing
  elif [ ! -f "$LIVE" ] && [ -f "$REAL" ]; then
    # The original is saved aside and nothing is live: the game will not start.
    # Both sibling installers report this and this one did not, so an install
    # interrupted between the move and the copy left the original stranded under
    # a name nothing looks for, while the app said "not patched yet" and greyed
    # out the one control that puts it back.
    echo half
  else echo absent
  fi
}

case "$MODE" in
--status)
  status
  exit 0
  ;;

--restore)
  state="$(status)"
  if [ "$state" = absent ]; then
    echo "the bridge is not installed, nothing to do"
    exit 0
  fi
  if [ "$state" = broken ]; then
    echo "error: $LIVE is our proxy but $REAL is missing." >&2
    echo "       Verify the game files in Steam to get the original back." >&2
    exit 1
  fi
  echo "[1/2] restoring the original libxess.dll"
  mv -f "$REAL" "$LIVE"
  echo "[2/2] done — the game is back to stock"
  ;;

--install|"")
  [ -f "$PROXY" ] || { echo "error: proxy DLL not found at $PROXY" >&2; exit 1; }

  if [ "$(status)" = installed ]; then
    echo "the bridge is already installed, nothing to do"
    exit 0
  fi

  # Half-installed: the original is saved aside and nothing is live. Installing
  # on top would move whatever is in its place over the saved original and lose
  # it. Restoring first is the only safe order, and the only one that leaves the
  # game able to start.
  if [ "$(status)" = half ]; then
    echo "error: this install is half finished — the original is at $REAL" >&2
    echo "       and nothing is in its place, so the game will not start." >&2
    echo "       Run with --restore first, then install again." >&2
    exit 1
  fi

  echo "[1/4] checking libxess.dll"
  # A game update or a Steam verification puts the stock DLL back, which leaves
  # any libxess_real.dll behind it stale.
  if is_ours "$LIVE"; then
    echo "error: $LIVE is already a proxy but $REAL is gone." >&2
    echo "       Verify the game files in Steam, then run this again." >&2
    exit 1
  fi

  echo "[2/4] checking the proxy exports everything the game imports"
  # If the game ships a libxess we do not fully forward, the process would fail
  # to start with a missing-entry-point error. Better to refuse now.
  #
  # Read each side separately and check both succeeded. Piping straight into
  # comm hides a failure: comm -23 with an unreadable left side reports nothing
  # missing, which reads exactly like "everything is forwarded" -- and the next
  # step then moves that unreadable file over the saved original. Whatever
  # $LIVE is, if its exports cannot be read it is not a DLL worth keeping.
  if ! live_exports="$(/usr/bin/perl "$EXPORTS" exports "$LIVE" 2>&1)"; then
    echo "error: cannot read the exports of $LIVE" >&2
    echo "$live_exports" | sed 's/^/       /' >&2
    if [ -f "$REAL" ]; then
      echo "       That file is damaged, and your original is still beside it." >&2
      echo "       Restore it before doing anything else:" >&2
      echo "         $0 \"$GAME\" --restore" >&2
    else
      echo "       Verify the game files in Steam, then run this again." >&2
    fi
    exit 1
  fi
  if ! proxy_exports="$(/usr/bin/perl "$EXPORTS" exports "$PROXY" 2>&1)"; then
    echo "error: cannot read the exports of the shipped proxy $PROXY" >&2
    echo "$proxy_exports" | sed 's/^/       /' >&2
    exit 1
  fi
  missing="$(comm -23 <(printf '%s\n' "$live_exports" | sort) \
                      <(printf '%s\n' "$proxy_exports" | sort))"
  if [ -n "$missing" ]; then
    echo "error: this build's libxess exports symbols the shipped proxy does not:" >&2
    echo "$missing" | sed 's/^/       /' >&2
    echo "       Rebuild it: runtime/build-proxy.sh \"$LIVE\" dwo-video-bridge.c" >&2
    exit 1
  fi

  echo "[3/4] moving the original aside as libxess_real.dll"
  # Nothing may write over $LIVE until the original is known to be safe under
  # $REAL. Without this the two commands are independent: on a directory that
  # denies rename but leaves the file writable -- a network library, a restored
  # backup, a deny-delete_child ACL -- the mv fails, the cp truncates the original
  # in place, and the script exits 0 printing "installed". Reproduced exactly that
  # way against a chmod 555 directory. There is no undo; the file is gone.
  mv -f "$LIVE" "$REAL" || {
    echo "error: could not move the original aside; nothing was changed" >&2
    exit 1
  }
  [ -f "$REAL" ] || {
    echo "error: the original is not where it should be; refusing to write" >&2
    exit 1
  }

  echo "[4/4] installing the bridge"
  cp "$PROXY" "$LIVE" || {
    echo "error: could not install the bridge; putting the original back" >&2
    mv -f "$REAL" "$LIVE" || echo "       and that failed too -- it is at $REAL" >&2
    exit 1
  }

  echo
  echo "installed — the cutscenes should play"
  echo "CrossOver must be patched with winevideo, or there is nothing to decode them"
  echo "a log is written to the bottle's C:\\${MARKER} on each launch"
  ;;

*)
  usage
  ;;
esac
