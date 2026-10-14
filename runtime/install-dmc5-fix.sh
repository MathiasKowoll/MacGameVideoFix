#!/usr/bin/env bash
#
# Install the Devil May Cry 5 video fix.
#
#     install-dmc5-fix.sh <game folder>            install
#     install-dmc5-fix.sh <game folder> --status   report
#     install-dmc5-fix.sh <game folder> --restore  undo
#
# The skill previews on the Customize screen are VC-1 in ASF, which no
# CrossOver decodes. Without a decoder Media Foundation refuses the stream, the
# game uses the reader it never got, and it crashes. The fix hands a refused
# stream to mgvf-asf-reader.dll, a reader built on FFmpeg (asf-reader.c),
# through mf-reader-fallback.c; a stream Media Foundation opens never reaches
# it, so the staged codec, where present, still plays first.
#
# It rides on amd_ags_x64.dll, which the game imports. Measured on CrossOver
# Preview 27 through NotProton, D3DMetal, nothing staged: the previews play.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# MGVF-SCOPE: folder
# MGVF-GAME: Devil May Cry 5 | DevilMayCry5.exe |
# MGVF-WHY: Crashes when a skill preview plays: the video is VC-1, which the fix decodes itself where the engine cannot.

set -euo pipefail

usage() { sed -n '3,7p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
# See install-p5s-bridge.sh: a read-only caller must never land on --install.
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

LIVE="$GAME/amd_ags_x64.dll"
REAL="$GAME/amd_ags_x64_real.dll"
PROXY="$HERE/amd_ags_x64-dmc5.dll"
READER="$HERE/mgvf-asf-reader.dll"
READER_LICENCES="$HERE/mgvf-asf-reader-LICENCES.md"
LIVE_READER="$GAME/mgvf-asf-reader.dll"
LIVE_LICENCES="$GAME/mgvf-asf-reader-LICENCES.md"
EXPORTS="$HERE/pe.pl"
MARKER='mgvf-mf-fallback.log'

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

[ -f "$GAME/DevilMayCry5.exe" ] || {
  echo "error: no DevilMayCry5.exe in $GAME" >&2
  echo "       Pick the folder Devil May Cry 5 is installed in." >&2
  exit 1
}

case "$MODE" in
--status)
  if is_ours "$LIVE" && [ -f "$REAL" ]; then
    # Either file out of date, or the reader missing, is one the app repairs
    # by installing again; it knows four words and "broken" is that one.
    if cmp -s "$LIVE" "$PROXY" && cmp -s "$LIVE_READER" "$READER"; then echo installed
    else echo "an older build of the fix is in place; install again" >&2; echo broken; fi
  elif is_ours "$LIVE"; then echo broken
  elif [ ! -f "$LIVE" ] && [ -f "$REAL" ]; then echo half
  else echo absent; fi
  exit 0
  ;;
--restore)
  rm -f "$LIVE_READER" "$LIVE_LICENCES"
  if [ -f "$REAL" ] && { [ ! -f "$LIVE" ] || is_ours "$LIVE"; }; then
    mv -f "$REAL" "$LIVE"
    echo "restored -- the game is back to its own amd_ags_x64.dll"
  elif [ -f "$REAL" ]; then
    echo "error: $LIVE is the game's own, not ours -- it was left alone." >&2
    echo "       $REAL is a leftover copy; remove it by hand if you want it gone." >&2
    exit 1
  else
    echo "nothing to restore"
  fi
  exit 0
  ;;
--install) ;;
*) usage ;;
esac

[ -f "$PROXY" ] && [ -f "$READER" ] || { echo "error: the fix's files are not beside this script" >&2; exit 1; }

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
  echo "[1/2] checking the proxy forwards everything the game imports"
  if ! live_exports="$(/usr/bin/perl "$EXPORTS" exports "$LIVE" 2>&1)"; then
    echo "error: cannot read the exports of $LIVE" >&2
    exit 1
  fi
  proxy_exports="$(/usr/bin/perl "$EXPORTS" exports "$PROXY")"
  missing="$(comm -23 <(printf '%s\n' "$live_exports" | sort) <(printf '%s\n' "$proxy_exports" | sort))"
  if [ -n "$missing" ]; then
    echo "error: this build's amd_ags_x64 exports symbols the shipped proxy does not:" >&2
    echo "$missing" | sed 's/^/       /' >&2
    exit 1
  fi
else
  echo "[1/2] the fix's amd_ags_x64.dll is in place"
fi

echo "[2/2] installing"
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
cp "$READER" "$LIVE_READER" && cp "$READER_LICENCES" "$LIVE_LICENCES" || {
  echo "error: could not copy the reader beside the game" >&2; exit 1; }
echo
echo "installed"
echo "  a video Media Foundation refuses goes to the fix's own reader; it writes $MARKER"
