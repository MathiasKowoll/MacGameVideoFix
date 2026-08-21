#!/usr/bin/env bash
#
# Launch a game under CrossOver with Media Foundation tracing on, and boil the
# result down to the lines that say why a video did not play.
#
# For the silent kind of failure: the game reaches the cutscene, the screen
# stays black, nothing crashes and no log is written. Media Foundation returns
# an HRESULT the game swallows, and the only way to see it is Wine's own trace.
#
#   capture-mf-trace.sh <bottle> <windows path to .exe> [more args...]
#
# e.g.
#   capture-mf-trace.sh SteamVp9 'Z:\Volumes\Disk\...\DWORIGINS.exe'
#
# If the game needs Steam, start Steam in that bottle first and leave it up.
#
# Part of MortalShell2MacFix — https://github.com/MathiasKowoll/MortalShell2MacFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -uo pipefail

usage() { sed -n '3,17p' "$0" >&2; exit 1; }
[ $# -ge 2 ] || usage

BOTTLE="$1"; EXE="$2"; shift 2

CX_APP="${CX_APP:-$HOME/Applications/CrossOver-winevideo.app}"
[ -d "$CX_APP" ] || CX_APP="/Applications/CrossOver.app"
WINE="$CX_APP/Contents/SharedSupport/CrossOver/bin/wine"
[ -x "$WINE" ] || { echo "error: no wine at $WINE (set CX_APP)" >&2; exit 1; }

OUT="${OUT:-$HOME/Desktop/mf-trace-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"
RAW="$OUT/raw.log"

# Channels, cheapest first. mfplat and mfreadwrite show what the game asks for;
# winegstreamer shows what the backend can actually deliver. quartz is left out
# on purpose -- almost nothing modern uses DirectShow, and it is loud.
export WINEDEBUG="${WINEDEBUG:-+mfplat,+mfreadwrite,+winegstreamer,+mferror}"

echo "engine : $CX_APP"
echo "bottle : $BOTTLE"
echo "trace  : $RAW"
echo
echo "Reach the point where the video should play, then quit the game."
echo

"$WINE" --bottle "$BOTTLE" --cx-app "$EXE" "$@" >"$OUT/stdout.log" 2>"$RAW"

echo
echo "==> $(wc -l < "$RAW" | tr -d ' ') lines captured"

# The needles. A silent video failure almost always shows up as a media type
# the backend refuses, a missing byte-stream handler, or a decoder that never
# gets created.
{
  echo "### errors and refusals"
  grep -aiE 'err:|0x8[0-9a-f]{7}|E_FAIL|NOTIMPL|_NOT_FOUND|UNSUPPORTED|INVALIDMEDIATYPE|CANNOT_FIND' "$RAW" |
    sort | uniq -c | sort -rn | head -60

  echo
  echo "### what the game asked Media Foundation for"
  grep -aiE 'SetCurrentMediaType|GetNativeMediaType|SetInputType|SetOutputType|MF_SOURCE_READER|D3D_MANAGER' "$RAW" | head -40

  echo
  echo "### byte stream handler / source resolution"
  grep -aiE 'byte_?stream|BeginCreateObject|CreateObjectFrom|resolve|webm|matroska' "$RAW" | head -40

  echo
  echo "### which decoders came up"
  grep -aiE 'MFTEnum|CreateTransform|decoder|vp9|vp8|wg_transform' "$RAW" | head -40
} > "$OUT/summary.txt"

echo "==> summary: $OUT/summary.txt"
sed -n '1,40p' "$OUT/summary.txt"
